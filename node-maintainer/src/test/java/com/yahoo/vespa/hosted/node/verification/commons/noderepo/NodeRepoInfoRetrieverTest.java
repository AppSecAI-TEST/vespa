package com.yahoo.vespa.hosted.node.verification.commons.noderepo;

import org.junit.Before;
import org.junit.Test;

import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.file.Paths;
import java.util.ArrayList;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

public class NodeRepoInfoRetrieverTest {

    private NodeRepoInfoRetriever nodeRepoInfoRetriever;
    private ArrayList<URL> urls;
    private static final double DELTA = 0.1;
    private static final String ABSOLUTE_PATH = Paths.get(".").toAbsolutePath().normalize().toString();
    private static final String RESOURCE_PATH = "src/test/java/com/yahoo/vespa/hosted/node/verification/spec/resources";
    private static final String URL_RESOURCE_PATH = "file://" + ABSOLUTE_PATH + "/" + RESOURCE_PATH;

    @Before
    public void setup() {
        nodeRepoInfoRetriever = new NodeRepoInfoRetriever();
        urls = new ArrayList<>();
    }

    @Test
    public void retrieve_should_return_nodeJSONModel_when_parameter_contains_valid_url() throws IOException {
        urls.add(new URL(URL_RESOURCE_PATH + "/nodeInfoTest.json"));
        NodeRepoJsonModel nodeRepoJsonModel = NodeRepoInfoRetriever.retrieve(urls);
        double expectedMinDiskAvailable = 500.0;
        double expectedMinMainMemoryAvailable = 24.0;
        double expectedMinCpuCores = 24.0;
        assertEquals(expectedMinDiskAvailable, nodeRepoJsonModel.getMinDiskAvailableGb(), DELTA);
        assertEquals(expectedMinMainMemoryAvailable, nodeRepoJsonModel.getMinMainMemoryAvailableGb(), DELTA);
        assertEquals(expectedMinCpuCores, nodeRepoJsonModel.getMinCpuCores(), DELTA);
    }

    @Test
    public void retrieve_should_throw_IOException_when_no_valid_URLs() throws MalformedURLException {
        urls = new ArrayList<>();
        String exceptionMessage = "Failed to parse JSON from all possible config servers.";
        try {
            NodeRepoInfoRetriever.retrieve(urls);
            fail("Retrieve should have thrown IOException");
        } catch (IOException e) {
            assertEquals(exceptionMessage, e.getMessage());
        }
        urls.add(new URL("file:///dev/null"));
        try {
            NodeRepoInfoRetriever.retrieve(urls);
            fail("Retrieve should have thrown IOException");
        } catch (IOException e) {
            assertEquals(exceptionMessage, e.getMessage());
        }
    }

}