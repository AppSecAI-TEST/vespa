// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.hosted.provision.provisioning;

import com.google.inject.Inject;
import com.yahoo.config.provision.ApplicationId;
import com.yahoo.config.provision.Capacity;
import com.yahoo.config.provision.ClusterSpec;
import com.yahoo.config.provision.HostFilter;
import com.yahoo.config.provision.HostSpec;
import com.yahoo.config.provision.NodeType;
import com.yahoo.config.provision.ProvisionLogger;
import com.yahoo.config.provision.Provisioner;
import com.yahoo.config.provision.Zone;
import com.yahoo.log.LogLevel;
import com.yahoo.transaction.NestedTransaction;
import com.yahoo.vespa.hosted.provision.Node;
import com.yahoo.vespa.hosted.provision.NodeRepository;
import com.yahoo.vespa.hosted.provision.node.Flavor;
import com.yahoo.vespa.hosted.provision.node.NodeFlavors;
import com.yahoo.vespa.hosted.provision.node.filter.ApplicationFilter;
import com.yahoo.vespa.hosted.provision.node.filter.NodeHostFilter;

import java.time.Clock;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Comparator;
import java.util.List;
import java.util.Optional;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * Implementation of the host provisioner API for hosted Vespa, using the node repository to allocate nodes.
 * Does not allocate hosts for the routing application, see VespaModelFactory.createHostProvisioner
 *
 * @author bratseth
 */
public class NodeRepositoryProvisioner implements Provisioner {

    private static Logger log = Logger.getLogger(NodeRepositoryProvisioner.class.getName());

    private final NodeRepository nodeRepository;
    private final CapacityPolicies capacityPolicies;
    private final Zone zone;
    private final Preparer preparer;
    private final Activator activator;

    @Inject
    public NodeRepositoryProvisioner(NodeRepository nodeRepository, NodeFlavors flavors, Zone zone) {
        this(nodeRepository, flavors, zone, Clock.systemUTC());
    }

    public NodeRepositoryProvisioner(NodeRepository nodeRepository, NodeFlavors flavors, Zone zone, Clock clock) {
        this.nodeRepository = nodeRepository;
        this.capacityPolicies = new CapacityPolicies(zone, flavors);
        this.zone = zone;
        this.preparer = new Preparer(nodeRepository, clock);
        this.activator = new Activator(nodeRepository, clock);
    }

    /**
     * Returns a list of nodes in the prepared or active state, matching the given constraints.
     * The nodes are ordered by increasing index number.
     */
    @Override
    public List<HostSpec> prepare(ApplicationId application, ClusterSpec cluster, Capacity requestedCapacity, 
                                  int wantedGroups, ProvisionLogger logger) {
        if (cluster.group().isPresent()) throw new IllegalArgumentException("Node requests cannot specify a group");
        if (requestedCapacity.nodeCount() > 0 && requestedCapacity.nodeCount() % wantedGroups != 0)
            throw new IllegalArgumentException("Requested " + requestedCapacity.nodeCount() + " nodes in " + wantedGroups + " groups, " +
                                               "which doesn't allow the nodes to be divided evenly into groups");

        log.log(LogLevel.DEBUG, () -> "Received deploy prepare request for " + requestedCapacity + " in " +
                                      wantedGroups + " groups for application " + application + ", cluster " + cluster);

        int effectiveGroups;
        NodeSpec requestedNodes;
        if ( requestedCapacity.type() == NodeType.tenant) {
            int nodeCount = capacityPolicies.decideSize(requestedCapacity);
            if (zone.environment().isManuallyDeployed() && nodeCount < requestedCapacity.nodeCount())
                logger.log(Level.INFO, "Requested " + requestedCapacity.nodeCount() + " nodes for " + cluster +
                                       ", downscaling to " + nodeCount + " nodes in " + zone.environment());
            Flavor flavor = capacityPolicies.decideFlavor(requestedCapacity, cluster);
            effectiveGroups = wantedGroups > nodeCount ? nodeCount : wantedGroups; // cannot have more groups than nodes
            requestedNodes = NodeSpec.from(nodeCount, flavor);
        }
        else {
            requestedNodes = NodeSpec.from(requestedCapacity.type());
            effectiveGroups = 1; // type request with multiple groups is not supported
        }

        return asSortedHosts(preparer.prepare(application, cluster, requestedNodes, effectiveGroups));
    }

    @Override
    public void activate(NestedTransaction transaction, ApplicationId application, Collection<HostSpec> hosts) {
        activator.activate(application, hosts, transaction);
    }

    @Override
    public void restart(ApplicationId application, HostFilter filter) {
        nodeRepository.restart(ApplicationFilter.from(application, NodeHostFilter.from(filter)));
    }

    @Override
    public void remove(NestedTransaction transaction, ApplicationId application) {
        nodeRepository.deactivate(application, transaction);
    }

    private List<HostSpec> asSortedHosts(List<Node> nodes) {
        nodes.sort(Comparator.comparingInt((Node node) -> node.allocation().get().membership().index()));
        List<HostSpec> hosts = new ArrayList<>(nodes.size());
        for (Node node : nodes) {
            hosts.add(new HostSpec(node.hostname(),
                                   node.allocation().orElseThrow(IllegalStateException::new).membership(),
                                   node.flavor().canonicalName()));
        }
        return hosts;
    }

}
