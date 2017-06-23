// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.hosted.provision.provisioning;

import com.yahoo.config.provision.Flavor;
import com.yahoo.vespa.hosted.provision.Node;

/**
 * Represent the capacity in terms of physical resources like memory, disk and cpu.
 * Can represent free, aggregate or total capacity of one or several nodes.
 *
 * @author smorgrav
 */
public class ResourceCapacity {

    private double memory;
    private double cpu;
    private double disk;

    public static ResourceCapacity add(ResourceCapacity a, ResourceCapacity b) {
        ResourceCapacity result = new ResourceCapacity();
        result.memory = a.memory + b.memory;
        result.cpu = a.cpu + b.cpu;
        result.disk = a.disk + b.disk;
        return result;
    }

    public static ResourceCapacity subtract(ResourceCapacity a, ResourceCapacity b) {
        ResourceCapacity result = new ResourceCapacity();
        result.memory = a.memory - b.memory;
        result.cpu = a.cpu - b.cpu;
        result.disk = a.disk - b.disk;
        return result;
    }

    public static ResourceCapacity of(Flavor flavor) {
        return new ResourceCapacity(flavor);
    }

    public static ResourceCapacity of(Node node) {
        return new ResourceCapacity(node);
    }

    ResourceCapacity() {
        memory = 0;
        cpu = 0;
        disk = 0;
    }

    ResourceCapacity(Node node) {
        memory = node.flavor().getMinMainMemoryAvailableGb();
        cpu = node.flavor().getMinCpuCores();
        disk = node.flavor().getMinDiskAvailableGb();
    }

    ResourceCapacity(Flavor flavor) {
        memory = flavor.getMinMainMemoryAvailableGb();
        cpu = flavor.getMinCpuCores();
        disk = flavor.getMinDiskAvailableGb();
    }

    public double getMemory() {
        return memory;
    }

    public double getCpu() {
        return cpu;
    }

    public double getDisk() {
        return disk;
    }

    public void subtract(Node node) {
        memory -= node.flavor().getMinMainMemoryAvailableGb();
        cpu -= node.flavor().getMinCpuCores();
        disk -= node.flavor().getMinDiskAvailableGb();
    }

    boolean hasCapacityFor(Flavor flavor) {
        return memory >= flavor.getMinMainMemoryAvailableGb() &&
                cpu >= flavor.getMinCpuCores() &&
                disk >= flavor.getMinDiskAvailableGb();
    }

    /**
     * @return True if that capacity is meet with this capacity - i.e this is bigger than that.
     */
    public boolean hasCapacityFor(ResourceCapacity that) {
        return memory >= that.memory &&
                cpu >= that.cpu &&
                disk >= that.disk;
    }

    int freeCapacityInFlavorEquivalence(Flavor flavor) {
        if (!hasCapacityFor(flavor)) return 0;

        double memoryFactor = Math.floor(memory/flavor.getMinMainMemoryAvailableGb());
        double cpuFactor = Math.floor(cpu/flavor.getMinCpuCores());
        double diskFactor =  Math.floor(disk/flavor.getMinDiskAvailableGb());

        double aggregateFactor = Math.min(memoryFactor, cpuFactor);
        aggregateFactor = Math.min(aggregateFactor, diskFactor);

        return (int)aggregateFactor;
    }

    /**
     * Normal compare implementation where -1 if this is less than that.
     */
    public int compare(ResourceCapacity that) {
        if (memory > that.memory) return 1;
        if (memory < that.memory) return -1;
        if (disk > that.disk) return 1;
        if (disk < that.disk) return -1;
        if (cpu > that.cpu) return 1;
        if (cpu < that.cpu) return -1;
        return 0;
    }

    Flavor asFlavor() {
        FlavorConfigBuilder b = new FlavorConfigBuilder();
        b.addFlavor("spareflavor", cpu, memory, disk, Flavor.Type.DOCKER_CONTAINER).idealHeadroom(1);
        return new Flavor(b.build().flavor(0));
    }
}
