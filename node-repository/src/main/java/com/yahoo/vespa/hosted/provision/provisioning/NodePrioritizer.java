package com.yahoo.vespa.hosted.provision.provisioning;

import com.yahoo.config.provision.ApplicationId;
import com.yahoo.config.provision.ClusterSpec;
import com.yahoo.config.provision.Flavor;
import com.yahoo.config.provision.NodeFlavors;
import com.yahoo.config.provision.NodeType;
import com.yahoo.vespa.hosted.provision.Node;
import com.yahoo.vespa.hosted.provision.NodeList;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;

/**
 * Builds up a priority queue of which nodes should be offered to the allocation.
 * <p>
 * Builds up a list of NodePriority objects and sorts them according to the
 * NodePriority::compare method.
 *
 * @author smorgrav
 */
public class NodePrioritizer {

    private final Map<Node, NodePriority> nodes = new HashMap<>();
    private final List<Node> allNodes;
    private final DockerHostCapacity capacity;
    private final NodeSpec requestedNodes;
    private final ApplicationId appId;
    private final ClusterSpec clusterSpec;

    private final boolean isAllocatingForReplacement;
    private final Set<Node> spareHosts;
    private final Map<Node, Boolean> headroomHosts;
    private final boolean isDocker;

    NodePrioritizer(List<Node> allNodes, ApplicationId appId, ClusterSpec clusterSpec, NodeSpec nodeSpec, NodeFlavors nodeFlavors, int spares) {
        this.allNodes = Collections.unmodifiableList(allNodes);
        this.requestedNodes = nodeSpec;
        this.clusterSpec = clusterSpec;
        this.appId = appId;

        spareHosts = findSpareHosts(allNodes, spares);
        headroomHosts = findHeadroomHosts(allNodes, spareHosts, nodeFlavors);

        this.capacity = new DockerHostCapacity(allNodes);

        long nofFailedNodes = allNodes.stream()
                .filter(node -> node.state().equals(Node.State.failed))
                .filter(node -> node.allocation().isPresent())
                .filter(node -> node.allocation().get().owner().equals(appId))
                .filter(node -> node.allocation().get().membership().cluster().id().equals(clusterSpec.id()))
                .count();

        long nofNodesInCluster = allNodes.stream()
                .filter(node -> node.allocation().isPresent())
                .filter(node -> node.allocation().get().owner().equals(appId))
                .filter(node -> node.allocation().get().membership().cluster().id().equals(clusterSpec.id()))
                .count();

        isAllocatingForReplacement = isReplacement(nofNodesInCluster, nofFailedNodes);
        isDocker = isDocker();
    }

    /**
     * From ipAddress - get hostname
     *
     * @return hostname or null if not able to do the loopup
     */
    private static String lookupHostname(String ipAddress) {
        try {
            return InetAddress.getByName(ipAddress).getHostName();
        } catch (UnknownHostException e) {
            e.printStackTrace();
        }
        return null;
    }

    private static Set<Node> findSpareHosts(List<Node> nodes, int spares) {
        DockerHostCapacity capacity = new DockerHostCapacity(new ArrayList<>(nodes));
        return nodes.stream()
                .filter(node -> node.type().equals(NodeType.host))
                .filter(dockerHost -> dockerHost.state().equals(Node.State.active))
                .filter(dockerHost -> capacity.freeIPs(dockerHost) > 0)
                .sorted(capacity::compareWithoutInactive)
                .limit(spares)
                .collect(Collectors.toSet());
    }

    private static Map<Node, Boolean> findHeadroomHosts(List<Node> nodes, Set<Node> spareNodes, NodeFlavors flavors) {
        DockerHostCapacity capacity = new DockerHostCapacity(nodes);
        Map<Node, Boolean> headroomNodesToViolation = new HashMap<>();

        List<Node> hostsSortedOnLeastCapacity = nodes.stream()
                .filter(n -> !spareNodes.contains(n))
                .filter(node -> node.type().equals(NodeType.host))
                .filter(dockerHost -> dockerHost.state().equals(Node.State.active))
                .filter(dockerHost -> capacity.freeIPs(dockerHost) > 0)
                .sorted((a, b) -> capacity.compareWithoutInactive(b, a))
                .collect(Collectors.toList());

        for (Flavor flavor : flavors.getFlavors().stream().filter(f -> f.getIdealHeadroom() > 0).collect(Collectors.toList())) {
            Set<Node> tempHeadroom = new HashSet<>();
            Set<Node> notEnoughCapacity = new HashSet<>();
            for (Node host : hostsSortedOnLeastCapacity) {
                if (headroomNodesToViolation.containsKey(host)) continue;
                if (capacity.hasCapacityWhenRetiredAndInactiveNodesAreGone(host, flavor)) {
                    headroomNodesToViolation.put(host, false);
                    tempHeadroom.add(host);
                } else {
                    notEnoughCapacity.add(host);
                }

                if (tempHeadroom.size() == flavor.getIdealHeadroom()) {
                    continue;
                }
            }

            // Now check if we have enough headroom - if not choose the nodes that almost has it
            if (tempHeadroom.size() < flavor.getIdealHeadroom()) {
                List<Node> violations = notEnoughCapacity.stream()
                        .sorted((a, b) -> capacity.compare(b, a))
                        .limit(flavor.getIdealHeadroom() - tempHeadroom.size())
                        .collect(Collectors.toList());

                // TODO should we be selective on which application on the node that violates the headroom?
                for (Node nodeViolatingHeadrom : violations) {
                    headroomNodesToViolation.put(nodeViolatingHeadrom, true);
                }

            }
        }

        return headroomNodesToViolation;
    }

    List<NodePriority> prioritize() {
        List<NodePriority> priorityList = new ArrayList<>(nodes.values());
        Collections.sort(priorityList, (a, b) -> NodePriority.compare(a, b));
        return priorityList;
    }

    void addSurplusNodes(List<Node> surplusNodes) {
        for (Node node : surplusNodes) {
            NodePriority nodePri = toNodePriority(node, true, false);
            if (!nodePri.violatesSpares || isAllocatingForReplacement) {
                nodes.put(node, nodePri);
            }
        }
    }

    void addNewDockerNodes() {
        if (!isDocker) return;
        DockerHostCapacity capacity = new DockerHostCapacity(allNodes);

        for (Node node : allNodes) {
            if (node.type() == NodeType.host) {
                boolean conflictingCluster = false;
                NodeList list = new NodeList(allNodes);
                NodeList childrenWithSameApp = list.childNodes(node).owner(appId);
                for (Node child : childrenWithSameApp.asList()) {
                    // Look for nodes from the same cluster
                    if (child.allocation().get().membership().cluster().id().equals(clusterSpec.id())) {
                        conflictingCluster = true;
                        break;
                    }
                }

                if (!conflictingCluster && capacity.hasCapacity(node, getFlavor())) {
                    Set<String> ipAddresses = DockerHostCapacity.findFreeIps(node, allNodes);
                    if (ipAddresses.isEmpty()) continue;
                    String ipAddress = ipAddresses.stream().findFirst().get();
                    String hostname = lookupHostname(ipAddress);
                    if (hostname == null) continue;
                    Node newNode = Node.createDockerNode("fake-" + hostname, Collections.singleton(ipAddress),
                            Collections.emptySet(), hostname, Optional.of(node.hostname()), getFlavor(), NodeType.tenant);
                    NodePriority nodePri = toNodePriority(newNode, false, true);
                    if (!nodePri.violatesSpares || isAllocatingForReplacement) {
                        nodes.put(newNode, nodePri);
                    }
                }
            }
        }
    }

    void addApplicationNodes() {
        List<Node.State> legalStates = Arrays.asList(Node.State.active, Node.State.inactive, Node.State.reserved);
        allNodes.stream()
                .filter(node -> node.type().equals(requestedNodes.type()))
                .filter(node -> legalStates.contains(node.state()))
                .filter(node -> node.allocation().isPresent())
                .filter(node -> node.allocation().get().owner().equals(appId))
                .map(node -> toNodePriority(node, false, false))
                .forEach(nodePriority -> nodes.put(nodePriority.node, nodePriority));
    }

    void addReadyNodes() {
        allNodes.stream()
                .filter(node -> node.type().equals(requestedNodes.type()))
                .filter(node -> node.state().equals(Node.State.ready))
                .map(node -> toNodePriority(node, false, false))
                .filter(n -> !n.violatesSpares || isAllocatingForReplacement)
                .forEach(nodePriority -> nodes.put(nodePriority.node, nodePriority));
    }

    /**
     * Convert a list of nodes to a list of node priorities. This includes finding, calculating
     * parameters to the priority sorting procedure.
     */
    private NodePriority toNodePriority(Node node, boolean isSurplusNode, boolean isNewNode) {
        NodePriority pri = new NodePriority();
        pri.node = node;
        pri.isSurplusNode = isSurplusNode;
        pri.isNewNode = isNewNode;
        pri.preferredOnFlavor = requestedNodes.specifiesNonStockFlavor() && node.flavor().equals(getFlavor());
        pri.parent = findParentNode(node);

        if (pri.parent.isPresent()) {
            Node parent = pri.parent.get();
            pri.freeParentCapacity = capacity.freeCapacityOf(parent, false);

            if (spareHosts.contains(parent)) {
                pri.violatesSpares = true;
            }

            if (headroomHosts.containsKey(parent)) {
                pri.violatesHeadroom = headroomHosts.get(parent);
            }
        }

        return pri;
    }

    private boolean isReplacement(long nofNodesInCluster, long nodeFailedNodes) {
        if (nodeFailedNodes == 0) return false;

        int wantedCount = 0;
        if (requestedNodes instanceof NodeSpec.CountNodeSpec) {
            NodeSpec.CountNodeSpec countSpec = (NodeSpec.CountNodeSpec) requestedNodes;
            wantedCount = countSpec.getCount();
        }

        return (wantedCount > nofNodesInCluster - nodeFailedNodes);
    }

    private Flavor getFlavor() {
        if (requestedNodes instanceof NodeSpec.CountNodeSpec) {
            NodeSpec.CountNodeSpec countSpec = (NodeSpec.CountNodeSpec) requestedNodes;
            return countSpec.getFlavor();
        }
        return null;
    }

    private boolean isDocker() {
        Flavor flavor = getFlavor();
        return (flavor != null) && flavor.getType().equals(Flavor.Type.DOCKER_CONTAINER);
    }

    private Optional<Node> findParentNode(Node node) {
        if (!node.parentHostname().isPresent()) return Optional.empty();
        return allNodes.stream()
                .filter(n -> n.hostname().equals(node.parentHostname().orElse(" NOT A NODE")))
                .findAny();
    }
}