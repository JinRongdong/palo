// Copyright (c) 2017, Baidu.com, Inc. All Rights Reserved

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef  BDG_PALO_BE_SRC_QUERY_EXEC_OLAP_SCAN_NODE_H
#define  BDG_PALO_BE_SRC_QUERY_EXEC_OLAP_SCAN_NODE_H

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/thread.hpp>
#include <queue>

#include "exec/olap_common.h"
#include "exec/olap_meta_reader.h"
#include "exec/olap_scanner.h"
#include "exec/scan_node.h"
#include "runtime/descriptors.h"
#include "runtime/row_batch_interface.hpp"
#include "runtime/vectorized_row_batch.h"
#include "util/progress_updater.h"
#include "util/debug_util.h"

namespace palo {

enum TransferStatus {
    READ_ROWBATCH = 1,
    INIT_HEAP = 2,
    BUILD_ROWBATCH = 3,
    MERGE = 4,
    FININSH = 5,
    ADD_ROWBATCH = 6,
    ERROR = 7
};

class OlapScanNode : public ScanNode {
public:
    OlapScanNode(ObjectPool* pool, const TPlanNode& tnode, const DescriptorTbl& descs);
    ~OlapScanNode();
    virtual Status init(const TPlanNode& tnode);
    virtual Status prepare(RuntimeState* state);
    virtual Status open(RuntimeState* state);
    virtual Status get_next(RuntimeState* state, RowBatch* row_batch, bool* eos);
    virtual Status close(RuntimeState* state);
    virtual Status set_scan_ranges(const std::vector<TScanRangeParams>& scan_ranges);

protected:
    typedef struct {
        Tuple* tuple;
        int id;
    } HeapType;
    class IsFixedValueRangeVisitor : public boost::static_visitor<bool> {
    public:
        template<class T>
        bool operator()(T& v) const {
            return v.is_fixed_value_range();
        }
    };

    class GetFixedValueSizeVisitor : public boost::static_visitor<size_t> {
    public:
        template<class T>
        size_t operator()(T& v) const {
            return v.get_fixed_value_size();
        }
    };

    class ExtendScanKeyVisitor : public boost::static_visitor<Status> {
    public:
        ExtendScanKeyVisitor(OlapScanKeys& scan_keys) : _scan_keys(scan_keys) { }
        template<class T>
        Status operator()(T& v) {
            return _scan_keys.extend_scan_key(v);
        }
    private:
        OlapScanKeys& _scan_keys;
    };

    typedef boost::variant<std::list<std::string>> string_list;

    class ToOlapFilterVisitor : public boost::static_visitor<std::string> {
    public:
        template<class T, class P>
        std::string operator()(T& v, P& v2) const {
            return v.to_olap_filter(v2);
        }
    };

    class MergeComparison {
    public:
        MergeComparison(CompareLargeFunc compute_fn, int offset) {
            _compute_fn = compute_fn;
            _offset = offset;
        }
        bool operator()(const HeapType& lhs, const HeapType& rhs) const {
            return (*_compute_fn)(lhs.tuple->get_slot(_offset), rhs.tuple->get_slot(_offset));
        }
    private:
        CompareLargeFunc _compute_fn;
        int _offset;
    };

    typedef std::priority_queue<HeapType, std::vector<HeapType>, MergeComparison> Heap;

    void display_heap(Heap& heap) {
        Heap h = heap;
        std::stringstream s;
        s << "Heap: [";

        while (!h.empty()) {
            HeapType v = h.top();
            s << "\nID: " << v.id << " Value:" << print_tuple(v.tuple, *_tuple_desc);
            h.pop();
        }

        VLOG(1) << s.str() << "\n]";
    }

    Status start_scan(RuntimeState* state);
    Status normalize_conjuncts();
    Status build_olap_filters();
    Status select_scan_ranges();
    Status build_scan_key();
    Status split_scan_range();
    Status start_scan_thread(RuntimeState* state);

    Status create_conjunct_ctxs(
            RuntimeState* state,
            std::vector<ExprContext*>* row_expr,
            std::vector<ExprContext*>* vec_expr,
            bool disable_codegen);

    template<class T>
    Status normalize_predicate(ColumnValueRange<T>& range, SlotDescriptor* slot);

    template<class T>
    Status normalize_in_predicate(SlotDescriptor* slot, ColumnValueRange<T>* range);

    template<class T>
    Status normalize_binary_predicate(SlotDescriptor* slot, ColumnValueRange<T>* range);

    bool select_scan_range(boost::shared_ptr<PaloScanRange> scan_range);
    Status get_sub_scan_range(
        boost::shared_ptr<PaloScanRange> scan_range,
        std::vector<OlapScanRange>* sub_range);
    void transfer_thread(RuntimeState* state);
    //void vectorized_scanner_thread(OlapScanner* scanner);
    void scanner_thread(OlapScanner* scanner);

    Status add_one_batch(RowBatchInterface* row_batch);
    Status transfer_open_scanners(RuntimeState* state);

    TransferStatus init_merge_heap(Heap& heap);
    TransferStatus read_row_batch(RuntimeState* state);
    TransferStatus build_row_batch(RuntimeState* state);
    TransferStatus sorted_merge(Heap& heap);

    void merge_transfer_thread(RuntimeState* state);

    // Write debug string of this into out.
    virtual void debug_string(int indentation_level, std::stringstream* out) const;

private:
    void construct_is_null_pred_in_where_pred(Expr* expr, SlotDescriptor* slot, std::string is_null_str);

    std::vector<TCondition> _is_null_vector;
    boost::scoped_ptr<TPlanNode> _thrift_plan_node;
    // Tuple id resolved in prepare() to set _tuple_desc;
    TupleId _tuple_id;
    // palo scan node used to scan palo
    TOlapScanNode _olap_scan_node;
    // tuple descriptors
    const TupleDescriptor* _tuple_desc;
    // tuple index
    int _tuple_idx;
    // string slots
    std::vector<SlotDescriptor*> _string_slots;

    bool _eos;

    // column -> ColumnValueRange map
    std::map<std::string, ColumnValueRangeType> _column_value_ranges;

    OlapScanKeys _scan_keys;

    std::list<boost::shared_ptr<PaloScanRange> > _palo_scan_ranges;

    std::vector<boost::shared_ptr<PaloScanRange> > _query_scan_ranges;
    std::vector<OlapScanRange> _query_key_ranges;

    std::vector<TCondition> _olap_filter;

    // Order Result Flag
    bool _is_result_order;
    // Result RowBatch order by this column
    std::string _sort_column;

    // Pool for storing allocated scanner objects.  We don't want to use the
    // runtime pool to ensure that the scanner objects are deleted before this
    // object is.
    boost::scoped_ptr<ObjectPool> _scanner_pool;

    // Thread group for transfer thread
    boost::thread_group _transfer_thread;
    // Keeps track of total splits and the number finished.
    ProgressUpdater _progress;

    // Lock and condition variables protecting _materialized_row_batches.  Row batches are
    // produced asynchronously by the scanner threads and consumed by the main thread in
    // GetNext.  Row batches must be processed by the main thread in the order they are
    // queued to avoid freeing attached resources prematurely (row batches will never depend
    // on resources attached to earlier batches in the queue).
    // This lock cannot be taken together with any other locks except _lock.
    boost::mutex _row_batches_lock;
    boost::condition_variable _row_batch_added_cv;
    boost::condition_variable _row_batch_consumed_cv;

    std::list<RowBatchInterface*> _materialized_row_batches;

    boost::mutex _scan_batches_lock;
    boost::condition_variable _scan_batch_added_cv;
    int32_t _scanner_task_finish_count;

    std::list<RowBatchInterface*> _scan_row_batches;

    std::list<OlapScanner*> _all_olap_scanners;
    std::list<OlapScanner*> _olap_scanners;
    std::vector<OlapScanner*> _fin_olap_scanners;

    // indicate which scanner need to read
    // -1 means all
    int _merge_scanner_id;

    // each scanner's RowBatch array, index with scanner id
    std::vector<std::list<RowBatch*> > _merge_rowbatches;

    // each scanner's lastest RowBatch removed from _merge_rowbatches
    // store here because it's lastest TupleRow still in heap
    // it will delete at ScanNode's destruct
    std::vector<RowBatch*> _backup_rowbatches;

    // first Rowbatch's row_idx of each _merge_rowbatches
    // >0 means row index
    // -1 means scanner has finished
    std::vector<int> _merge_row_idxs;

    // finish flag of each scanner
    std::vector<bool> _scanner_fin_flags;

    // present RowBatch MergeTransferThread processing
    RowBatch* _merge_rowbatch;

    // present TupleRow MergeTransferThread processing
    Tuple* _merge_tuple;

    int _max_materialized_row_batches;
    bool _start;
    bool _scanner_done;
    bool _transfer_done;
    bool _use_pushdown_conjuncts;
    size_t _direct_conjunct_size;
    size_t _direct_row_conjunct_size;
    size_t _direct_vec_conjunct_size;

    boost::posix_time::time_duration _wait_duration;
    bool _delete;
    int _total_assign_num;
    int _nice;

    // protect _status, for many thread may change _status
    boost::mutex _status_mutex;
    Status _status;
    RuntimeState* _runtime_state;
    RuntimeProfile::Counter* _olap_thread_scan_timer;
    RuntimeProfile::Counter* _eval_timer;
    RuntimeProfile::Counter* _merge_timer;
    RuntimeProfile::Counter* _pushdown_return_counter;
    RuntimeProfile::Counter* _direct_return_counter;
    RuntimeProfile::Counter* _tablet_counter;

    RuntimeProfile* _scanner_profile;

    TResourceInfo* _resource_info;

    int64_t _buffered_bytes;
    int64_t _running_thread;
    EvalConjunctsFn _eval_conjuncts_fn;
};

} // namespace palo

#endif
