// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.hosted.node.admin.docker;

import com.yahoo.net.HostName;
import static com.yahoo.vespa.defaults.Defaults.getDefaults;
import com.yahoo.vespa.hosted.dockerapi.Container;
import com.yahoo.vespa.hosted.dockerapi.ContainerName;
import com.yahoo.vespa.hosted.dockerapi.Docker;
import com.yahoo.vespa.hosted.dockerapi.DockerImage;
import com.yahoo.vespa.hosted.dockerapi.DockerImpl;
import com.yahoo.vespa.hosted.dockerapi.DockerTestUtils;
import com.yahoo.vespa.hosted.dockerapi.ProcessResult;
import com.yahoo.vespa.hosted.node.admin.ContainerNodeSpec;
import com.yahoo.vespa.hosted.node.admin.noderepository.NodeRepositoryImpl;
import com.yahoo.vespa.hosted.node.admin.noderepository.bindings.GetNodesResponse;
import com.yahoo.vespa.hosted.node.admin.util.ConfigServerHttpRequestExecutor;
import com.yahoo.vespa.hosted.node.admin.util.Environment;
import com.yahoo.vespa.hosted.provision.Node;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.URL;
import java.net.UnknownHostException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;

/**
 * @author freva
 */
public class LocalZoneUtils {
    public static final int CONFIG_SERVER_WEB_SERVICE_PORT = 4080;
    public static final String CONFIG_SERVER_HOSTNAME = "config-server";
    public static final ContainerName CONFIG_SERVER_CONTAINER_NAME = new ContainerName(CONFIG_SERVER_HOSTNAME);
    public static final String NODE_ADMIN_HOSTNAME = getParentHostHostname();
    public static final ContainerName NODE_ADMIN_CONTAINER_NAME = new ContainerName("node-admin");

    private static final ConfigServerHttpRequestExecutor requestExecutor = ConfigServerHttpRequestExecutor.create(
            Collections.singleton(CONFIG_SERVER_HOSTNAME));
    private static final String APP_HOSTNAME_PREFIX = "cnode-";
    private static final String TENANT_NAME = "localtenant";
    private static final String APPLICATION_NAME = "default";

    public static void startConfigServerIfNeeded(Docker docker, Environment environment, DockerImage dockerImage, Path pathToProjectRoot) throws UnknownHostException {
        Optional<Container> container = docker.getContainer(CONFIG_SERVER_CONTAINER_NAME);
        if (container.isPresent()) {
            if (container.get().state.isRunning()) return;
            else docker.deleteContainer(CONFIG_SERVER_CONTAINER_NAME);
        }

        Path pathToConfigServerApp = Paths.get(getDefaults().underVespaHome("conf/configserver-app"));
        docker.createContainerCommand(dockerImage, CONFIG_SERVER_CONTAINER_NAME, CONFIG_SERVER_HOSTNAME)
                .withNetworkMode(DockerImpl.DOCKER_CUSTOM_MACVLAN_NETWORK_NAME)
                .withIpAddress(environment.getInetAddressForHost(CONFIG_SERVER_HOSTNAME))
                .withEnvironment("HOSTED_VESPA_ENVIRONMENT", environment.getEnvironment())
                .withEnvironment("HOSTED_VESPA_REGION", environment.getRegion())
                .withEnvironment("CONFIG_SERVER_HOSTNAME", CONFIG_SERVER_HOSTNAME)
                .withUlimit("nofile", 262_144, 262_144)
                .withUlimit("nproc", 32_768, 409_600)
                .withUlimit("core", -1, -1)
                .withEntrypoint(pathToConfigServerApp.resolve("start-config-server.sh").toString())
                .create();

        docker.copyArchiveToContainer(pathToProjectRoot.resolve("node-admin/configserver-app").toString(),
                CONFIG_SERVER_CONTAINER_NAME, getDefaults().underVespaHome("conf"));

        docker.startContainer(CONFIG_SERVER_CONTAINER_NAME);
    }

    public static void startNodeAdminIfNeeded(Docker docker, Environment environment, DockerImage dockerImage, Path pathToContainerStorage) {
        Optional<Docker.ContainerStats> containerStats = docker.getContainerStats(NODE_ADMIN_CONTAINER_NAME);
        if (containerStats.isPresent())
            return;
        else
            docker.deleteContainer(NODE_ADMIN_CONTAINER_NAME);

        Docker.CreateContainerCommand createCmd = docker.createContainerCommand(dockerImage,
                NODE_ADMIN_CONTAINER_NAME, NODE_ADMIN_HOSTNAME)
                .withNetworkMode("host")
                .withVolume("/proc", "/host/proc")
                .withVolume("/var/run/docker.sock", "/host/var/run/docker.sock")
                .withVolume(pathToContainerStorage.toString(), "/host" + pathToContainerStorage.toString())
                .withEnvironment("ENVIRONMENT", environment.getEnvironment())
                .withEnvironment("REGION", environment.getRegion())
                .withEnvironment("CONFIG_SERVER_ADDRESS", CONFIG_SERVER_HOSTNAME)
                .withEnvironment("COREDUMP_FEED_ENDPOINT", "http://feed-endpoint.hostname.tld/feed")
                .withEnvironment("RUNNING_LOCALLY", "true")
                .withUlimit("nofile", 262_144, 262_144)
                .withUlimit("nproc", 32_768, 409_600)
                .withUlimit("core", -1, -1)
                .withEntrypoint("/usr/local/bin/start-services.sh", "--run-local");

        if (DockerTestUtils.getSystemOS() == DockerTestUtils.OS.Mac_OS_X) {
            createCmd.withNetworkMode(DockerImpl.DOCKER_CUSTOM_MACVLAN_NETWORK_NAME);
        }

        Arrays.asList(
                "logs/daemontools_y",
                "logs/jdisc_core",
                "logs/langdetect/",
                "logs/vespa",
                "logs/yca",
                "logs/yck",
                "logs/yell",
                "logs/ykeykey",
                "logs/ykeykeyd",
                "logs/yms_agent",
                "logs/ysar",
                "logs/ystatus",
                "logs/zpe_policy_updater",
                "var/cache",
                "var/crash",
                "var/db/jdisc",
                "var/db/vespa",
                "var/jdisc_container",
                "var/jdisc_core",
                "var/maven",
                "var/run",
                "var/scoreboards",
                "var/service",
                "var/share",
                "var/spool",
                "var/vespa",
                "var/yca",
                "var/ycore++",
                "var/zookeeper")
            .forEach(path -> {
                                  String absPath = getDefaults().underVespaHome(path);
                                  Path resolved = pathToContainerStorage.resolve("node-admin" + absPath);
                                  createCmd.withVolume(resolved.toString(), absPath);
                              });

        createCmd.create();
        docker.startContainer(NODE_ADMIN_CONTAINER_NAME);
        docker.executeInContainerAsRoot(NODE_ADMIN_CONTAINER_NAME, "chown", getDefaults().vespaUser(), "/host/var/run/docker.sock");
    }

    public static Optional<ContainerNodeSpec> getContainerNodeSpec(String hostName) {
        try {
            GetNodesResponse.Node nodeResponse = requestExecutor.get("/nodes/v2/node/" + hostName,
                    CONFIG_SERVER_WEB_SERVICE_PORT, GetNodesResponse.Node.class);
            if (nodeResponse == null) {
                return Optional.empty();
            }
            return Optional.of(NodeRepositoryImpl.createContainerNodeSpec(nodeResponse));
        } catch (ConfigServerHttpRequestExecutor.NotFoundException e) {
            return Optional.empty();
        }
    }

    public static void provisionHost(String hostname) {
        List<Map<String, String>> nodesToAdd = new ArrayList<>();
        Map<String, String> provisionNodeRequest = new HashMap<>();
        provisionNodeRequest.put("type", "host");
        provisionNodeRequest.put("flavor", "docker");
        provisionNodeRequest.put("hostname", hostname);
        provisionNodeRequest.put("openStackId", "fake-" + hostname);
        nodesToAdd.add(provisionNodeRequest);

        try {
            requestExecutor.post("/nodes/v2/node", CONFIG_SERVER_WEB_SERVICE_PORT, nodesToAdd, Map.class);
        } catch (RuntimeException e) {
            if (! e.getMessage().contains("A node with this name already exists")) throw e;
        }
    }

    /**
     * Adds numberOfNodes to node-repo and returns a set of node hostnames.
     */
    public static Set<String> provisionNodes(String parentHostname, int numberOfNodes) {
        List<Map<String, Object>> nodesToAdd = new ArrayList<>();
        for (int i = 1; i <= numberOfNodes; i++) {
            final String hostname = APP_HOSTNAME_PREFIX + i;
            Map<String, Object> provisionNodeRequest = new HashMap<>();
            provisionNodeRequest.put("parentHostname", parentHostname);
            provisionNodeRequest.put("type", "tenant");
            provisionNodeRequest.put("flavor", "docker");
            provisionNodeRequest.put("hostname", hostname);
            provisionNodeRequest.put("ipAddresses", Collections.singletonList("172.18.2." + i));
            provisionNodeRequest.put("openStackId", "fake-" + hostname);
            nodesToAdd.add(provisionNodeRequest);
        }

        try {
            requestExecutor.post("/nodes/v2/node", CONFIG_SERVER_WEB_SERVICE_PORT, nodesToAdd, Map.class);
        } catch (RuntimeException e) {
            if (! e.getMessage().contains("A node with this name already exists")) throw e;
        }
        return nodesToAdd.stream().map(i -> (String) i.get("hostname")).collect(Collectors.toSet());
    }

    public static void setState(Node.State state, String hostname) {
        try {
            requestExecutor.put("/nodes/v2/state/" + state + "/" + hostname,
                    CONFIG_SERVER_WEB_SERVICE_PORT, Optional.empty(), Map.class);
        } catch (RuntimeException e) {
            if (! e.getMessage().contains("Not registered as provisioned, dirty, failed or parked")) throw e;
        }
    }

    public static void deployApp(Docker docker, Path pathToApp) {
        deployApp(docker, pathToApp, TENANT_NAME, APPLICATION_NAME);
    }

    public static void deployApp(Docker docker, Path pathToApp, String tenantName, String applicationName) {
        Path pathToAppOnConfigServer = Paths.get("/tmp").resolve(pathToApp.getFileName());
        docker.copyArchiveToContainer(pathToApp.toString(), CONFIG_SERVER_CONTAINER_NAME, pathToAppOnConfigServer.getParent().toString() + "/");

        try { // Add tenant, ignore exception if tenant already exists
            requestExecutor.put("/application/v2/tenant/" + tenantName, CONFIG_SERVER_WEB_SERVICE_PORT, Optional.empty(), Map.class);
        } catch (RuntimeException e) {
            if (! e.getMessage().contains("There already exists a tenant '" + tenantName)) {
                throw e;
            }
        }
        System.out.println("prepare " + applicationName);
        final String deployPath = getDefaults().underVespaHome("bin/vespa-deploy");
        ProcessResult copyProcess = docker.executeInContainer(CONFIG_SERVER_CONTAINER_NAME, deployPath, "-e",
                tenantName, "-a", applicationName, "prepare", pathToAppOnConfigServer.toString());
        if (! copyProcess.isSuccess()) {
            throw new RuntimeException("Could not prepare " + pathToApp + " on " + CONFIG_SERVER_CONTAINER_NAME.asString() +
                    "\n" + copyProcess.getOutput() + "\n" + copyProcess.getErrors());
        }

        System.out.println("activate " + applicationName);
        ProcessResult execProcess = docker.executeInContainer(CONFIG_SERVER_CONTAINER_NAME, deployPath, "-e",
                tenantName, "-a", applicationName, "activate");
        if (! execProcess.isSuccess()) {
            throw new RuntimeException("Could not activate application\n" + copyProcess.getOutput() + "\n" + copyProcess.getErrors());
        }
    }

    public static Set<String> getContainersForApp() {
        return getContainersForApp(TENANT_NAME, APPLICATION_NAME, "default");
    }

    @SuppressWarnings("unchecked")
    public static Set<String> getContainersForApp(String tenant, String application, String instance) {
        String app = String.join(".", tenant, application, instance);
        Map<String, Object> response = requestExecutor.get("/nodes/v2/node/?recursive=true&clusterType=container&application=" + app,
                CONFIG_SERVER_WEB_SERVICE_PORT, Map.class);
        List<Map<String, Object>> nodes = (List<Map<String, Object>>) response.get("nodes");
        return nodes.stream().map(nodeMap -> (String) nodeMap.get("hostname")).collect(Collectors.toSet());
    }

    public static void packageApp(Path pathToApp) {
        try {
            Process process = Runtime.getRuntime().exec("mvn package", null, pathToApp.toFile());
            BufferedReader buff = new BufferedReader(new InputStreamReader(process.getInputStream()));

            String line;
            while((line = buff.readLine()) != null) System.out.println(line);

            assert process.waitFor() == 0;
        } catch (IOException | InterruptedException e) {
            throw new RuntimeException("Failed to package application", e);
        }
    }

    public static void deleteApplication() {
        deleteApplication(TENANT_NAME, APPLICATION_NAME);
    }

    public static void deleteApplication(String tenantName, String appName) {
        requestExecutor.delete("/application/v2/tenant/" + tenantName + "/application/" + appName,
                CONFIG_SERVER_WEB_SERVICE_PORT, Map.class);
    }

    public static boolean isReachableURL(URL url, Duration timeout) {
        Instant start = Instant.now();
        while (Instant.now().minus(timeout).isBefore(start)) {
            try {
                Thread.sleep(100);
                HttpURLConnection http = (HttpURLConnection) url.openConnection();
                if (http.getResponseCode() == 200) return true;
            } catch (IOException | InterruptedException ignored) { }
        }

        return false;
    }

    private static String getParentHostHostname() {
        String hostname = HostName.getLocalhost();
        // Avoid issue with hostname not being FQDN on MacOs
        if (DockerTestUtils.getSystemOS() == DockerTestUtils.OS.Mac_OS_X) {
            try {
                InetAddress ipAddress = InetAddress.getByName(hostname);
                return InetAddress.getByAddress(ipAddress.getAddress()).getHostName();
            } catch (UnknownHostException e) {
                throw new RuntimeException("Could not find hostname");
            }
        }
        return hostname;
    }
}

