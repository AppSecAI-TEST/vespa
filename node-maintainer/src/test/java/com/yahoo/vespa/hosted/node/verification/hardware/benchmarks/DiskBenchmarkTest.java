package com.yahoo.vespa.hosted.node.verification.hardware.benchmarks;

import com.yahoo.vespa.hosted.node.verification.commons.parser.ParseResult;
import com.yahoo.vespa.hosted.node.verification.mock.MockCommandExecutor;
import org.junit.Before;
import org.junit.Test;

import java.util.ArrayList;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

/**
 * Created by sgrostad on 12/07/2017.
 */
public class DiskBenchmarkTest {

    private DiskBenchmark diskBenchmark;
    private BenchmarkResults benchmarkResults;
    private MockCommandExecutor commandExecutor;
    private static final String VALID_OUTPUT_FILE = "src/test/java/com/yahoo/vespa/hosted/node/verification/hardware/resources/diskBenchmarkValidOutput";
    private static final String INVALID_OUTPUT_FILE = "src/test/java/com/yahoo/vespa/hosted/node/verification/hardware/resources/diskBenchmarkInvalidOutput";
    private static final double DELTA = 0.1;

    @Before
    public void setup() {
        commandExecutor = new MockCommandExecutor();
        benchmarkResults = new BenchmarkResults();
        diskBenchmark = new DiskBenchmark(benchmarkResults, commandExecutor);
    }

    @Test
    public void doBenchmark_should_store_diskSpeed_when_valid_output() {
        String mockCommand = "cat " + VALID_OUTPUT_FILE;
        commandExecutor.addCommand(mockCommand);
        diskBenchmark.doBenchmark();
        double expectedSpeed = 243;
        double actualSpeed = benchmarkResults.getDiskSpeedMbs();
        assertEquals(expectedSpeed, actualSpeed, DELTA);
    }

    @Test
    public void doBenchmark_should_store_diskSpeed_as_zero_when_invalid_output() {
        String mockCommand = "cat " + INVALID_OUTPUT_FILE;
        commandExecutor.addCommand(mockCommand);
        diskBenchmark.doBenchmark();
        double expectedSpeed = 0;
        double actualSpeed = benchmarkResults.getDiskSpeedMbs();
        assertEquals(expectedSpeed, actualSpeed, DELTA);
    }


    @Test
    public void parseDiskSpeed_valid_input() throws Exception {
        ArrayList<String> mockCommandOutput = MockCommandExecutor.readFromFile(VALID_OUTPUT_FILE);
        ParseResult parseResult = diskBenchmark.parseDiskSpeed(mockCommandOutput);
        ParseResult expectedParseResult = new ParseResult("MB/s", "243");
        assertEquals(expectedParseResult, parseResult);
    }

    @Test
    public void parseDiskSpeed_invalid_input() throws Exception {
        ArrayList<String> mockCommandOutput = MockCommandExecutor.readFromFile(INVALID_OUTPUT_FILE);
        ParseResult parseResult = diskBenchmark.parseDiskSpeed(mockCommandOutput);
        ParseResult expectedParseResult = new ParseResult("invalid", "invalid");
        assertEquals(expectedParseResult, parseResult);
    }

    @Test
    public void setDiskSpeed_valid_input() {
        ParseResult parseResult = new ParseResult("MB/s", "243");
        diskBenchmark.setDiskSpeed(parseResult);
        double expectedDiskSpeed = 243;
        assertEquals(expectedDiskSpeed, benchmarkResults.getDiskSpeedMbs(), DELTA);
    }

    @Test
    public void setDiskSpeed_invalid_input() {
        ParseResult parseResult = new ParseResult("invalid", "invalid");
        diskBenchmark.setDiskSpeed(parseResult);
        double expectedDiskSpeed = 0;
        assertEquals(expectedDiskSpeed, benchmarkResults.getDiskSpeedMbs(), DELTA);
    }

    @Test
    public void getDiskSpeedInMBs_for_KBs_MBs_and_GBs() {
        ParseResult KBsParseResult = new ParseResult("kB/s", "243000");
        ParseResult MBsParseResult = new ParseResult("MB/s", "243");
        ParseResult GBsParseResult = new ParseResult("GB/s", "0.243");
        double expectedMBs = 243;
        assertEquals(expectedMBs, diskBenchmark.getDiskSpeedInMBs(KBsParseResult), DELTA);
        assertEquals(expectedMBs, diskBenchmark.getDiskSpeedInMBs(MBsParseResult), DELTA);
        assertEquals(expectedMBs, diskBenchmark.getDiskSpeedInMBs(GBsParseResult), DELTA);
    }

    @Test
    public void ckeckSpeedValidity_should_return_true_for_valid_format() {
        String speed = "123";
        assertTrue(diskBenchmark.checkSpeedValidity(speed));
        speed = "30000";
        assertTrue(diskBenchmark.checkSpeedValidity(speed));
        speed = "6";
        assertTrue(diskBenchmark.checkSpeedValidity(speed));
    }

    @Test
    public void ckeckSpeedValidity_should_return_false_for_valid_format() {
        String speed = "124 GHz";
        assertFalse(diskBenchmark.checkSpeedValidity(speed));
        speed = null;
        assertFalse(diskBenchmark.checkSpeedValidity(speed));
        speed = "This should return false as well";
        assertFalse(diskBenchmark.checkSpeedValidity(speed));
    }

    @Test
    public void convertToMbs_should_return_properly_converted_disk_speeds() {
        String speed = "1234";
        double factor = 1000;
        double expectedSpeed = 1234000;
        assertEquals(expectedSpeed, diskBenchmark.convertToMBs(speed, factor), DELTA);
        factor = 1 / 1000.0;
        expectedSpeed = 1.234;
        assertEquals(expectedSpeed, diskBenchmark.convertToMBs(speed, factor), DELTA);
        factor = 1;
        expectedSpeed = 1234;
        assertEquals(expectedSpeed, diskBenchmark.convertToMBs(speed, factor), DELTA);
    }

}