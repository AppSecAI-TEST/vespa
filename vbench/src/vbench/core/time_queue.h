// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "closeable.h"
#include <vespa/vespalib/util/priority_queue.h>
#include <vespa/vespalib/util/sync.h>
#include <memory>

namespace vbench {

/**
 * A thread-safe priority queue keeping track of objects queued
 * according to an abstract time line. After a time queue is closed,
 * all incoming objects will be deleted.
 **/
template <typename T>
class TimeQueue : public Closeable
{
private:
    struct Entry {
        std::unique_ptr<T> object;
        double time;
        Entry(std::unique_ptr<T> obj, double t) : object(std::move(obj)), time(t) {}
        Entry(Entry &&rhs) : object(std::move(rhs.object)), time(rhs.time) {}
        Entry &operator=(Entry &&rhs) {
            object = std::move(rhs.object);
            time = rhs.time;
            return *this;
        }
        bool operator<(const Entry &rhs) const {
            return (time < rhs.time);
        }
    };

    vespalib::Monitor              _monitor;
    double                         _time;
    double                         _window;
    double                         _tick;
    vespalib::PriorityQueue<Entry> _queue;
    bool                           _closed;

public:
    TimeQueue(double window, double tick);
    void close() override;
    void discard();
    void insert(std::unique_ptr<T> obj, double time);
    bool extract(double time, std::vector<std::unique_ptr<T> > &list, double &delay);
};

} // namespace vbench

#include "time_queue.hpp"

