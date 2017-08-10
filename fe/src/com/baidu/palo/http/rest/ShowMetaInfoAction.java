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

package com.baidu.palo.http.rest;

import com.baidu.palo.catalog.Catalog;
import com.baidu.palo.catalog.Database;
import com.baidu.palo.catalog.MaterializedIndex;
import com.baidu.palo.catalog.OlapTable;
import com.baidu.palo.catalog.Partition;
import com.baidu.palo.catalog.Replica;
import com.baidu.palo.catalog.Replica.ReplicaState;
import com.baidu.palo.catalog.Table;
import com.baidu.palo.catalog.Table.TableType;
import com.baidu.palo.catalog.Tablet;
import com.baidu.palo.common.Config;
import com.baidu.palo.ha.HAProtocol;
import com.baidu.palo.http.ActionController;
import com.baidu.palo.http.BaseRequest;
import com.baidu.palo.http.BaseResponse;
import com.baidu.palo.http.IllegalArgException;
import com.baidu.palo.persist.Storage;
import com.google.gson.Gson;

import io.netty.handler.codec.http.HttpMethod;

import org.apache.commons.lang.StringUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class ShowMetaInfoAction extends RestBaseAction {
    private enum Action {
        SHOW_DB_SIZE,
        SHOW_HA,
        INVALID;
        
        public static Action getAction(String str) {
            try {
                return valueOf(str);
            } catch (Exception ex) {
                return INVALID;
            }
        }
    }

    private static final Logger LOG = LogManager.getLogger(ShowMetaInfoAction.class);

    public ShowMetaInfoAction(ActionController controller) {
        super(controller);
    }

    public static void registerAction(ActionController controller) throws IllegalArgException {
        controller.registerHandler(HttpMethod.GET, "/api/show_meta_info",
                                   new ShowMetaInfoAction(controller));
    }

    @Override
    public void execute(BaseRequest request, BaseResponse response) {
        String action = request.getSingleParameter("action");
        Gson gson = new Gson();
        response.setContentType("application/json");

        switch (Action.getAction(action.toUpperCase())) {
            case SHOW_DB_SIZE:
                response.getContent().append(gson.toJson(getDataSize()));
                break;
            case SHOW_HA:
                response.getContent().append(gson.toJson(getHaInfo()));
                break;
            default:
                break;
        }
        sendResult(request, response);
    }
    
    public Map<String, String> getHaInfo() {
        HashMap<String, String> feInfo = new HashMap<String, String>();
        feInfo.put("role", Catalog.getInstance().getFeType().toString());
        if (Catalog.getInstance().isMaster()) {
            feInfo.put("current_journal_id",
                       String.valueOf(Catalog.getInstance().getEditLog().getMaxJournalId()));
        } else {
            feInfo.put("current_journal_id",
                       String.valueOf(Catalog.getInstance().getReplayedJournalId()));
        }

        HAProtocol haProtocol = Catalog.getInstance().getHaProtocol();
        if (haProtocol != null) {
            InetSocketAddress master = haProtocol.getLeader();
            if (master != null) {
                feInfo.put("master", master.getHostString());
            }

            List<InetSocketAddress> electableNodes = haProtocol.getElectableNodes(false);
            ArrayList<String> electableNodeNames = new ArrayList<String>();
            if (electableNodes != null) {
                for (InetSocketAddress node : electableNodes) {
                    electableNodeNames.add(node.getHostString());
                }
                feInfo.put("electable_nodes",
                        StringUtils.join(electableNodeNames.toArray(), ","));
            }

            List<InetSocketAddress> observerNodes = haProtocol.getObserverNodes();
            ArrayList<String> observerNodeNames = new ArrayList<String>();
            if (observerNodes != null) {
                for (InetSocketAddress node : observerNodes) {
                    observerNodeNames.add(node.getHostString());
                }
                feInfo.put("observer_nodes", StringUtils.join(observerNodeNames.toArray(), ","));
            }
        }

        feInfo.put("can_read", String.valueOf(Catalog.getInstance().canRead()));
        try {
            Storage storage = new Storage(Config.meta_dir + "/image");
            feInfo.put("last_checkpoint_version", String.valueOf(storage.getImageSeq()));
            long lastCheckpointTime = storage.getCurrentImageFile().lastModified();
            feInfo.put("last_checkpoint_time", String.valueOf(lastCheckpointTime));
        } catch (IOException e) {
            LOG.warn(e.getMessage());
        }
        return feInfo;
    }

    public Map<String, Long> getDataSize() {
        Map<String, Long> result = new HashMap<String, Long>();
        List<String> dbNames = Catalog.getInstance().getDbNames();

        for (int i = 0; i < dbNames.size(); i++) {
            String dbName = dbNames.get(i);
            Database db = Catalog.getInstance().getDb(dbName);

            long totalSize = 0;
            List<Table> tables = db.getTables();
            for (int j = 0; j < tables.size(); j++) {
                Table table = tables.get(j);
                if (table.getType() != TableType.OLAP) {
                    continue;
                }

                OlapTable olapTable = (OlapTable) table;
                long tableSize = 0;
                for (Partition partition : olapTable.getPartitions()) {
                    long partitionSize = 0;
                    for (MaterializedIndex mIndex : partition.getMaterializedIndices()) {
                        long indexSize = 0;
                        for (Tablet tablet : mIndex.getTablets()) {
                            long maxReplicaSize = 0;
                            for (Replica replica : tablet.getReplicas()) {
                                if (replica.getState() == ReplicaState.NORMAL
                                        || replica.getState() == ReplicaState.SCHEMA_CHANGE) {
                                    if (replica.getDataSize() > maxReplicaSize) {
                                        maxReplicaSize = replica.getDataSize();
                                    }
                                }
                            } // end for replicas
                            indexSize += maxReplicaSize;
                        } // end for tablets
                        partitionSize += indexSize;
                    } // end for tables
                    tableSize += partitionSize;
                } // end for partitions
                totalSize += tableSize;
            } // end for tables
            result.put(dbName, Long.valueOf(totalSize));
        } // end for dbs
        return result;
    }
}
