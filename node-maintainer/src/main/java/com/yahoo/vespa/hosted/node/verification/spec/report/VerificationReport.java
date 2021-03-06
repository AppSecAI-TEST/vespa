package com.yahoo.vespa.hosted.node.verification.spec.report;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yahoo.vespa.hosted.node.verification.spec.retrievers.HardwareInfo;


/**
 * Stores results of caomparing node repo spec and actual hardware info.
 * In case of divergent values, set the corresponding attribute to the actual hardware info value.
 * Attributes of equal value remain null.
 */
@JsonInclude(JsonInclude.Include.NON_NULL)
public class VerificationReport {

    @JsonProperty
    private Double actualMemoryAvailable;
    @JsonProperty
    private HardwareInfo.DiskType actualDiskType;
    @JsonProperty
    private Double actualDiskSpaceAvailable;
    @JsonProperty
    private Double actualInterfaceSpeed;
    @JsonProperty
    private Integer actualcpuCores;
    @JsonProperty
    private String[] faultyIpAddresses;
    @JsonProperty
    private Boolean actualIpv6Connection;

    public void setActualIpv6Connection(boolean actualIpv6Connection) {
        this.actualIpv6Connection = actualIpv6Connection;
    }

    public void setActualMemoryAvailable(Double actualMemoryAvailable) {
        this.actualMemoryAvailable = actualMemoryAvailable;
    }

    public void setActualDiskType(HardwareInfo.DiskType actualFastDisk) {
        this.actualDiskType = actualFastDisk;
    }

    public void setActualDiskSpaceAvailable(Double actualDiskSpaceAvailable) {
        this.actualDiskSpaceAvailable = actualDiskSpaceAvailable;
    }

    public void setActualcpuCores(int actualcpuCores) {
        this.actualcpuCores = actualcpuCores;
    }

    public void setActualInterfaceSpeed(Double actualInterfaceSpeed) {
        this.actualInterfaceSpeed = actualInterfaceSpeed;
    }

    public void setFaultyIpAddresses(String[] faultyIpAddresses) {
        this.faultyIpAddresses = faultyIpAddresses;
    }
}
