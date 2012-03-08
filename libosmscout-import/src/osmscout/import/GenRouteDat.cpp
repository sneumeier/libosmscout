/*
  This source is part of the libosmscout library
  Copyright (C) 2012  Tim Teulings

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include <osmscout/import/GenRouteDat.h>

#include <algorithm>
#include <cassert>

#include <osmscout/DataFile.h>

#include <osmscout/RouteNode.h>

#include <osmscout/Util.h>

#include <osmscout/util/Geometry.h>
#include <osmscout/util/StopClock.h>
#include <osmscout/util/String.h>

#include <osmscout/import/RawNode.h>
#include <osmscout/import/RawRelation.h>
#include <osmscout/import/RawWay.h>

namespace osmscout {

  static uint8_t CopyFlags(const Way& way)
  {
    uint8_t flags=0;

    if (way.HasAccess()) {
      flags|=RouteNode::hasAccess;
    }

    return flags;
  }


  std::string RouteDataGenerator::GetDescription() const
  {
    return "Generate 'route.dat'";
  }

  bool RouteDataGenerator::ReadRestrictionRelations(const ImportParameter& parameter,
                                                    Progress& progress,
                                                    const TypeConfig& typeConfig,
                                                    std::map<Id,std::vector<Restriction> >& restrictions)
  {
    FileScanner      scanner;
    uint32_t         rawRelCount=0;
    std::set<TypeId> posRestrictions;
    std::set<TypeId> negRestrictions;

    if (typeConfig.GetRelationTypeId("restriction_only_right_turn")!=typeIgnore) {
      posRestrictions.insert(typeConfig.GetRelationTypeId("restriction_only_right_turn"));
    }

    if (typeConfig.GetRelationTypeId("restriction_only_left_turn")!=typeIgnore) {
      posRestrictions.insert(typeConfig.GetRelationTypeId("restriction_only_left_turn"));
    }

    if (typeConfig.GetRelationTypeId("restriction_only_straight_on")!=typeIgnore) {
      posRestrictions.insert(typeConfig.GetRelationTypeId("restriction_only_straight_on"));
    }

    if (typeConfig.GetRelationTypeId("restriction_no_right_turn")!=typeIgnore) {
      negRestrictions.insert(typeConfig.GetRelationTypeId("restriction_no_right_turn"));
    }

    if (typeConfig.GetRelationTypeId("restriction_no_left_turn")!=typeIgnore) {
      negRestrictions.insert(typeConfig.GetRelationTypeId("restriction_no_left_turn"));
    }

    if (typeConfig.GetRelationTypeId("restriction_no_u_turn")!=typeIgnore) {
      negRestrictions.insert(typeConfig.GetRelationTypeId("restriction_no_u_turn"));
    }

    if (typeConfig.GetRelationTypeId("restriction_no_straight_on")!=typeIgnore) {
      negRestrictions.insert(typeConfig.GetRelationTypeId("restriction_no_straight_on"));
    }

    if (!scanner.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                      "rawrels.dat"))) {
      progress.Error("Cannot open 'rawrels.dat'");
      return false;
    }

    if (!scanner.Read(rawRelCount)) {
      progress.Error("Error while reading number of data entries in file");
      return false;
    }

    for (uint32_t r=1; r<=rawRelCount; r++) {
      progress.SetProgress(r,rawRelCount);

      RawRelation relation;

      if (!relation.Read(scanner)) {
        progress.Error(std::string("Error while reading data entry ")+
                       NumberToString(r)+" of "+
                       NumberToString(rawRelCount)+
                       " in file '"+
                       scanner.GetFilename()+"'");
        return false;
      }

      if (posRestrictions.find(relation.GetType())!=posRestrictions.end() ||
          negRestrictions.find(relation.GetType())!=negRestrictions.end()) {
        Id          via=0;
        Restriction restriction;

        if (posRestrictions.find(relation.GetType())!=posRestrictions.end()) {
          restriction.type=allow;
        }
        else if (negRestrictions.find(relation.GetType())!=negRestrictions.end()) {
          restriction.type=forbit;
        }
        else {
          continue;
        }

        for (size_t i=0; i<relation.members.size(); i++) {
          if (relation.members[i].type==RawRelation::memberWay &&
              relation.members[i].role=="from") {
            restriction.from=relation.members[i].id;
          }
          else if (relation.members[i].type==RawRelation::memberNode &&
                   relation.members[i].role=="via") {
            via=relation.members[i].id;
          }
          else if (relation.members[i].type==RawRelation::memberWay &&
                   relation.members[i].role=="to") {
            restriction.to=relation.members[i].id;
          }
        }

        if (restriction.from!=0 && via!=0 && restriction.to!=0) {
          restrictions[via].push_back(restriction);
        }
      }
    }

    if (!scanner.Close()) {
      progress.Error("Cannot close file 'rawrels.dat'");
      return false;
    }

    progress.Info(std::string("Found ")+NumberToString(restrictions.size())+" restrictions");

    return true;
  }

  bool RouteDataGenerator::CanTurn(const std::vector<Restriction>& restrictions,
                                   Id from,
                                   Id to) const
  {
    bool defaultReturn=true;

    if (restrictions.empty()) {
      return true;
    }

    for (std::vector<Restriction>::const_iterator iter=restrictions.begin();
         iter!=restrictions.end();
         ++iter) {
      if (iter->from==from) {
        if (iter->type==allow) {
          if (iter->to==to) {
            return true;
          }

          // If there are allow restrictions,everything else is forbidden
          defaultReturn=false;
        }
        else if (iter->type==forbit) {
          if (iter->to==to) {
            return false;
          }

          // If there are forbid restrictions, everything else is allowed
          defaultReturn=true;
        }
      }
    }

    // Now lets hope nobody is mixing allow and forbid!
    return defaultReturn;
  }

  bool RouteDataGenerator::ReadJunctions(const ImportParameter& parameter,
                                         Progress& progress,
                                         const TypeConfig& typeConfig,
                                         std::set<Id>& junctions)
  {
    FileScanner         scanner;
    uint32_t            wayCount=0;
    std::map<Id,size_t> nodeWayCountMap;

    if (!scanner.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                      "ways.dat"),
                                      true,
                                      parameter.GetWayDataMemoryMaped())) {
      progress.Error("Cannot open 'ways.dat'");
      return false;
    }

    if (!scanner.Read(wayCount)) {
      progress.Error("Error while reading number of data entries in file");
      return false;
    }

    for (uint32_t w=1; w<=wayCount; w++) {
      progress.SetProgress(w,wayCount);

      Way way;

      if (!way.Read(scanner)) {
        progress.Error(std::string("Error while reading data entry ")+
                       NumberToString(w)+" of "+
                       NumberToString(wayCount)+
                       " in file '"+
                       scanner.GetFilename()+"'");
        return false;
      }

      if (way.GetType()==typeIgnore) {
        continue;
      }

      if (typeConfig.GetTypeInfo(way.GetType()).GetIgnore()) {
        continue;
      }

      if (!typeConfig.GetTypeInfo(way.GetType()).CanBeRoute()) {
        continue;
      }

      for (std::vector<Point>::const_iterator node=way.nodes.begin();
          node!=way.nodes.end();
          node++) {
        std::map<Id,size_t>::iterator entry=nodeWayCountMap.find(node->GetId());

        if (entry!=nodeWayCountMap.end()) {
          entry->second++;
        }
        else {
         nodeWayCountMap[node->GetId()]=1;
        }
      }
    }

    if (!scanner.Close()) {
      progress.Error("Cannot close file 'ways.dat'");
      return false;
    }

    for (std::map<Id,size_t>::iterator node=nodeWayCountMap.begin();
        node!=nodeWayCountMap.end();) {
      if (node->second>=2) {
        junctions.insert(node->first);
      }

      nodeWayCountMap.erase(node++);
    }

    return true;
  }

  bool RouteDataGenerator::ReadWayEndpoints(const ImportParameter& parameter,
                                            Progress& progress,
                                            const TypeConfig& typeConfig,
                                            const std::set<Id>& junctions,
                                            std::map<Id,std::list<Id> >& endPointWayMap)
  {
    FileScanner scanner;
    uint32_t    wayCount=0;

    if (!scanner.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                      "ways.dat"),
                                      true,
                                      parameter.GetWayDataMemoryMaped())) {
      progress.Error("Cannot open 'ways.dat'");
      return false;
    }

    if (!scanner.Read(wayCount)) {
      progress.Error("Error while reading number of data entries in file");
      return false;
    }

    for (uint32_t w=1; w<=wayCount; w++) {
      progress.SetProgress(w,wayCount);

      Way way;

      if (!way.Read(scanner)) {
        progress.Error(std::string("Error while reading data entry ")+
                       NumberToString(w)+" of "+
                       NumberToString(wayCount)+
                       " in file '"+
                       scanner.GetFilename()+"'");
        return false;
      }

      if (way.GetType()==typeIgnore) {
        continue;
      }

      if (typeConfig.GetTypeInfo(way.GetType()).GetIgnore()) {
        continue;
      }

      if (!typeConfig.GetTypeInfo(way.GetType()).CanBeRoute()) {
        continue;
      }

      for (std::vector<Point>::const_iterator node=way.nodes.begin();
          node!=way.nodes.end();
          node++) {
        if (junctions.find(node->GetId())!=junctions.end()) {
          endPointWayMap[node->GetId()].push_back(way.GetId());
        }
      }
    }

    if (!scanner.Close()) {
      progress.Error("Cannot close file 'ways.dat'");
      return false;
    }

    return true;
  }

  bool RouteDataGenerator::LoadWays(Progress& progress,
                                    FileScanner& scanner,
                                    NumericIndex<Id,Way>& wayIndex,
                                    const std::set<Id>& ids,
                                    std::list<WayRef>& ways)
  {
    std::vector<FileOffset> offsets;

    if (!wayIndex.GetOffsets(ids,offsets)) {
      return false;
    }

    FileOffset oldPos;

    if (!scanner.GetPos(oldPos)){
      progress.Error("Error while getting current file position");
      return false;
    }

    for (std::vector<FileOffset>::const_iterator offset=offsets.begin();
         offset!=offsets.end();
         offset++) {
      if (!scanner.SetPos(*offset)) {
        progress.Error("Error while moving to way at offset " + NumberToString(*offset));
        return false;
      }

      WayRef way=new Way();

      way->Read(scanner);

      if (scanner.HasError()) {
        progress.Error("Error while loading way at offset " + NumberToString(*offset));
        return false;
      }

      ways.push_back(way);
    }

    if (!scanner.SetPos(oldPos)) {
      progress.Error("Error while resetting current file position");
      return false;
    }

    return true;
  }

  bool RouteDataGenerator::Import(const ImportParameter& parameter,
                                  Progress& progress,
                                  const TypeConfig& typeConfig)
  {
    progress.SetAction("Generate route.dat");

    FileScanner                            scanner;
    FileWriter                             writer;

    // List of restrictions for a way
    std::map<Id,std::vector<Restriction> > restrictions;

    uint32_t                               handledRouteNodeCount=0;
    uint32_t                               writtenRouteNodeCount=0;
    uint32_t                               writtenRoutePathCount=0;

    std::set<Id>                           junctions;
    std::map<Id,std::list<Id> >            nodeWayMap;

    //
    // Handling of restriction relations
    //

    progress.SetAction("Scanning for restriction relations");

    if (!ReadRestrictionRelations(parameter,
                                  progress,
                                  typeConfig,
                                  restrictions)) {
      return false;
    }

    //
    // Building a map of nodes and the number of ways that contain this way
    //

    progress.SetAction("Scanning for junctions");

    if (!ReadJunctions(parameter,
                       progress,
                       typeConfig,
                       junctions)) {
      return false;
    }

    progress.Info(NumberToString(junctions.size())+ " junctions found");

    //
    // Building a map of way endpoint ids and list of ways having this point as endpoint
    //

    progress.SetAction("Collecting ways intersecting junctions");

    if (!ReadWayEndpoints(parameter,
                          progress,
                          typeConfig,
                          junctions,
                          nodeWayMap)) {
      return false;
    }

    junctions.clear();

    progress.Info(NumberToString(nodeWayMap.size())+ " route nodes collected");

    NumericIndex<Id,Way> wayIndex("way.idx",parameter.GetWayIndexCacheSize());

    if (!wayIndex.Open(parameter.GetDestinationDirectory(),
                           parameter.GetWayIndexMemoryMaped())) {
      std::cerr << "Cannot open way index file!" << std::endl;
      return false;
    }

    //
    // Writing route nodes
    //

    progress.SetAction("Writing route nodes");

    if (!scanner.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                      "ways.dat"),
                                      true,
                                      parameter.GetWayDataMemoryMaped())) {
      progress.Error("Cannot open 'ways.dat'");
      return false;
    }

    if (!writer.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                     "route.dat"))) {
      progress.Error("Cannot create 'route.dat'");
      return false;
    }

    writer.Write(writtenRouteNodeCount);

    std::vector<std::map<Id,std::list<Id> >::iterator> block(parameter.GetRouteNodeBlockSize());

    std::map<Id,std::list<Id> >::iterator node=nodeWayMap.begin();
    while (node!=nodeWayMap.end()) {

      // Fill the current block of nodes to be processed

      size_t blockCount=0;

      progress.SetAction("Loading up to " + NumberToString(block.size()) + " route nodes");
      while (blockCount<block.size() &&
             node!=nodeWayMap.end()) {
        progress.SetProgress(writtenRouteNodeCount,nodeWayMap.size());

        block[blockCount]=node;

        blockCount++;
        node++;
      }

      progress.SetAction("Loading intersecting ways");

      // Collect way ids of all ways in current block and load them

      std::set<Id>      wayIds;
      std::list<WayRef> ways;

      for (size_t w=0; w<blockCount; w++) {
        for (std::list<Id>::const_iterator wayId=block[w]->second.begin();
            wayId!=block[w]->second.end();
            wayId++) {
          wayIds.insert(*wayId);
        }
      }

      if (wayIds.empty()) {
        continue;
      }

      if (!LoadWays(progress,
                    scanner,
                    wayIndex,
                    wayIds,
                    ways)) {
        return false;
      }

      progress.SetAction("Storing route nodes");

      // Put all ways into a map by id

      std::map<Id,WayRef> waysMap;

      for (std::list<WayRef>::const_iterator way=ways.begin();
          way!=ways.end();
          way++) {
        waysMap[(*way)->GetId()]=*way;
      }

      for (size_t w=0; w<blockCount; w++) {
        std::map<Id,std::list<Id> >::iterator node=block[w];
        RouteNode                             routeNode;

        routeNode.id=node->first;

        handledRouteNodeCount++;
        progress.SetProgress(handledRouteNodeCount,nodeWayMap.size());

        // We sort ways by increasing id, for more efficient storage
        // in route node
        node->second.sort();

        for (std::list<Id>::const_iterator wayId=node->second.begin();
            wayId!=node->second.end();
            wayId++) {
          const WayRef& way=waysMap[*wayId];

          if (!way.Valid()) {
            progress.Error("Error while loading way "+
                           NumberToString(*wayId) +
                           " (Internal error?)");
            continue;
          }

          routeNode.ways.push_back(*wayId);

          // Area routing
          if (way->IsArea()) {
            int    currentNode=0;
            double distance;

            while (currentNode<(int)way->nodes.size() &&
                  way->nodes[currentNode].GetId()!=node->first) {
              currentNode++;
            }

            assert(currentNode<(int)way->nodes.size());

            int nextNode=currentNode+1;

            if (nextNode>=(int)way->nodes.size()) {
              nextNode=0;
            }

            distance=GetSphericalDistance(way->nodes[currentNode].GetLon(),
                                          way->nodes[currentNode].GetLat(),
                                          way->nodes[nextNode].GetLon(),
                                          way->nodes[nextNode].GetLat());

            while (nextNode!=currentNode &&
                nodeWayMap.find(way->nodes[nextNode].GetId())==nodeWayMap.end()) {
              int lastNode=nextNode;
              nextNode++;

              if (nextNode>=(int)way->nodes.size()) {
                nextNode=0;
              }

              if (nextNode!=currentNode) {
                distance+=GetSphericalDistance(way->nodes[lastNode].GetLon(),
                                               way->nodes[lastNode].GetLat(),
                                               way->nodes[nextNode].GetLon(),
                                               way->nodes[nextNode].GetLat());
              }
            }

            if (nextNode!=currentNode) {
              RouteNode::Path path;

              path.id=way->nodes[nextNode].GetId();
              path.wayIndex=routeNode.ways.size()-1;
              path.type=way->GetType();
              path.maxSpeed=way->GetMaxSpeed();
              path.flags=CopyFlags(*way);
              path.lat=way->nodes[nextNode].GetLat();
              path.lon=way->nodes[nextNode].GetLon();
              path.distance=distance;

              routeNode.paths.push_back(path);
            }

            int prevNode=currentNode-1;

            if (prevNode<0) {
              prevNode=(int)(way->nodes.size()-1);
            }

            distance=GetSphericalDistance(way->nodes[currentNode].GetLon(),
                                          way->nodes[currentNode].GetLat(),
                                          way->nodes[prevNode].GetLon(),
                                          way->nodes[prevNode].GetLat());

            while (prevNode!=currentNode &&
                nodeWayMap.find(way->nodes[prevNode].GetId())==nodeWayMap.end()) {
              int lastNode=prevNode;
              prevNode--;

              if (prevNode<0) {
                prevNode=(int)(way->nodes.size()-1);
              }

              if (prevNode!=currentNode) {
                distance+=GetSphericalDistance(way->nodes[lastNode].GetLon(),
                                               way->nodes[lastNode].GetLat(),
                                               way->nodes[prevNode].GetLon(),
                                               way->nodes[prevNode].GetLat());
              }
            }

            if (prevNode!=currentNode &&
                prevNode!=nextNode) {
              RouteNode::Path path;

              path.id=way->nodes[prevNode].GetId();
              path.wayIndex=routeNode.ways.size()-1;
              path.type=way->GetType();
              path.maxSpeed=way->GetMaxSpeed();
              path.flags=CopyFlags(*way);
              path.lat=way->nodes[prevNode].GetLat();
              path.lon=way->nodes[prevNode].GetLon();
              path.distance=distance;

              routeNode.paths.push_back(path);
            }
          }
          // Circular way routing (similar to current area routing, but respecting isOneway())
          else if (way->nodes.front().GetId()==way->nodes.back().GetId()) {
            int    currentNode=0;
            double distance;

            while (currentNode<(int)way->nodes.size() &&
                  way->nodes[currentNode].GetId()!=node->first) {
              currentNode++;
            }

            assert(currentNode<(int)way->nodes.size());

            int nextNode=currentNode+1;

            if (nextNode>=(int)way->nodes.size()) {
              nextNode=0;
            }

            distance=GetSphericalDistance(way->nodes[currentNode].GetLon(),
                                          way->nodes[currentNode].GetLat(),
                                          way->nodes[nextNode].GetLon(),
                                          way->nodes[nextNode].GetLat());

            while (nextNode!=currentNode &&
                nodeWayMap.find(way->nodes[nextNode].GetId())==nodeWayMap.end()) {
              int lastNode=nextNode;
              nextNode++;

              if (nextNode>=(int)way->nodes.size()) {
                nextNode=0;
              }

              if (nextNode!=currentNode) {
                distance+=GetSphericalDistance(way->nodes[lastNode].GetLon(),
                                               way->nodes[lastNode].GetLat(),
                                               way->nodes[nextNode].GetLon(),
                                               way->nodes[nextNode].GetLat());
              }
            }

            if (nextNode!=currentNode) {
              RouteNode::Path path;

              path.id=way->nodes[nextNode].GetId();
              path.wayIndex=routeNode.ways.size()-1;
              path.type=way->GetType();
              path.maxSpeed=way->GetMaxSpeed();
              path.flags=CopyFlags(*way);
              path.lat=way->nodes[nextNode].GetLat();
              path.lon=way->nodes[nextNode].GetLon();
              path.distance=distance;

              routeNode.paths.push_back(path);
            }

            if (!way->IsOneway()) {
              int prevNode=currentNode-1;

              if (prevNode<0) {
                prevNode=(int)(way->nodes.size()-1);
              }

              distance=GetSphericalDistance(way->nodes[currentNode].GetLon(),
                                            way->nodes[currentNode].GetLat(),
                                            way->nodes[prevNode].GetLon(),
                                            way->nodes[prevNode].GetLat());

              while (prevNode!=currentNode &&
                  nodeWayMap.find(way->nodes[prevNode].GetId())==nodeWayMap.end()) {
                int lastNode=prevNode;
                prevNode--;

                if (prevNode<0) {
                  prevNode=(int)(way->nodes.size()-1);
                }

                if (prevNode!=currentNode) {
                  distance+=GetSphericalDistance(way->nodes[lastNode].GetLon(),
                                                 way->nodes[lastNode].GetLat(),
                                                 way->nodes[prevNode].GetLon(),
                                                 way->nodes[prevNode].GetLat());
                }
              }

              if (prevNode!=currentNode &&
                  prevNode!=nextNode) {
                RouteNode::Path path;

                path.id=way->nodes[prevNode].GetId();
                path.wayIndex=routeNode.ways.size()-1;
                path.type=way->GetType();
                path.maxSpeed=way->GetMaxSpeed();
                path.flags=CopyFlags(*way);
                path.lat=way->nodes[prevNode].GetLat();
                path.lon=way->nodes[prevNode].GetLon();
                path.distance=distance;

                routeNode.paths.push_back(path);
              }
            }
          }
          // Normal way routing
          else {
            for (size_t i=0; i<way->nodes.size(); i++) {
              if (way->nodes[i].GetId()==node->first) {
                if (i>0 && !way->IsOneway()) {
                  int j=i-1;

                  while (j>=0) {
                    if (nodeWayMap.find(way->nodes[j].GetId())!=nodeWayMap.end()) {
                      break;
                    }

                    j--;
                  }

                  if (j>=0) {
                    RouteNode::Path path;

                    path.id=way->nodes[j].GetId();
                    path.wayIndex=routeNode.ways.size()-1;
                    path.type=way->GetType();
                    path.maxSpeed=way->GetMaxSpeed();
                    path.flags=CopyFlags(*way);
                    path.lat=way->nodes[j].GetLat();
                    path.lon=way->nodes[j].GetLon();

                    path.distance=0.0;
                    for (size_t d=j;d<i; d++) {
                      path.distance+=GetSphericalDistance(way->nodes[d].GetLon(),
                                                          way->nodes[d].GetLat(),
                                                          way->nodes[d+1].GetLon(),
                                                          way->nodes[d+1].GetLat());
                    }

                    routeNode.paths.push_back(path);
                  }
                }

                if (i+1<way->nodes.size()) {
                  size_t j=i+1;

                  while (j<way->nodes.size()) {
                    if (nodeWayMap.find(way->nodes[j].GetId())!=nodeWayMap.end()) {
                      break;
                    }

                    j++;
                  }

                  if (j<way->nodes.size()) {
                    RouteNode::Path path;

                    path.id=way->nodes[j].GetId();
                    path.wayIndex=routeNode.ways.size()-1;
                    path.type=way->GetType();
                    path.maxSpeed=way->GetMaxSpeed();
                    path.flags=CopyFlags(*way);
                    path.lat=way->nodes[j].GetLat();
                    path.lon=way->nodes[j].GetLon();

                    path.distance=0.0;
                    for (size_t d=i;d<j; d++) {
                      path.distance+=GetSphericalDistance(way->nodes[d].GetLon(),
                                                          way->nodes[d].GetLat(),
                                                          way->nodes[d+1].GetLon(),
                                                          way->nodes[d+1].GetLat());
                    }

                    routeNode.paths.push_back(path);
                  }
                }
              }
            }
          }
        }

        std::map<Id,std::vector<Restriction> >::const_iterator turnConstraints=restrictions.find(routeNode.GetId());

        if (turnConstraints!=restrictions.end()) {
          for (std::list<Id>::const_iterator sourceWayId=node->second.begin();
              sourceWayId!=node->second.end();
              sourceWayId++) {
            for (std::list<Id>::const_iterator destWayId=node->second.begin();
                destWayId!=node->second.end();
                destWayId++) {
              if (*sourceWayId!=*destWayId) {
                if (!CanTurn(turnConstraints->second,*sourceWayId,*destWayId)) {
                  RouteNode::Exclude exclude;

                  exclude.sourceWay=*sourceWayId;
                  exclude.targetPath=0;

                  while (exclude.targetPath<routeNode.paths.size() &&
                      routeNode.ways[routeNode.paths[exclude.targetPath].wayIndex]!=*destWayId) {
                    exclude.targetPath++;
                  }

                  if (exclude.targetPath<routeNode.paths.size()) {
                    routeNode.excludes.push_back(exclude);
                  }
                }
              }
            }
          }
        }

        if (!routeNode.Write(writer)) {
          return false;
        }

        writtenRouteNodeCount++;
        writtenRoutePathCount+=routeNode.paths.size();

        node++;
      }
    }

    if (!scanner.Close()) {
      progress.Error("Cannot close file 'route.dat'");
      return false;
    }

    writer.SetPos(0);
    writer.Write(writtenRouteNodeCount);


    if (!writer.Close()) {
      return false;
    }

    if (!wayIndex.Close()) {
      return false;
    }

    progress.Info(NumberToString(writtenRouteNodeCount) + " route node(s) and " + NumberToString(writtenRoutePathCount)+ " paths written");

    // Cleaning up...

    nodeWayMap.clear();
    restrictions.clear();

    return true;
  }
}
