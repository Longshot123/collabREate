/*
   collabREate db_mgr.cpp
   Copyright (C) 2018 Chris Eagle <cseagle at gmail d0t com>
   Copyright (C) 2018 Tim Vidas <tvidas at gmail d0t com>

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License along with
   this program; if not, write to the Free Software Foundation, Inc., 59 Temple
   Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <string>
#include <map>
#include <vector>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <openssl/md5.h>
#include <json-c/json.h>

#include "utils.h"
#include "db_mgr.h"
#include "proj_info.h"
#include "clientset.h"

using namespace std;

uint8_t *HmacMD5(const uint8_t *msg, int mlen, const uint8_t *key, int klen) {
   uint8_t ipad[64];
   uint8_t opad[64];
   uint8_t md5[MD5_DIGEST_LENGTH];
   memset(ipad, 0, sizeof(ipad));
   memcpy(ipad, key, klen);
   memcpy(opad, ipad, sizeof(ipad));
   
   /* XOR key with ipad and opad values */
   for (int i = 0; i < sizeof(ipad); i++) {
      ipad[i] ^= 0x36;
      opad[i] ^= 0x5c;
   }
   
   // perform inner MD5
   MD5_CTX ctx;
   MD5_Init(&ctx);
   MD5_Update(&ctx, ipad, sizeof(ipad));
   MD5_Update(&ctx, msg, mlen);
   MD5_Final(md5, &ctx);
   
   // perform outer MD5
   MD5_Init(&ctx);
   MD5_Update(&ctx, opad, sizeof(opad));
   MD5_Update(&ctx, md5, sizeof(md5));
   uint8_t *res = new uint8_t[MD5_DIGEST_LENGTH];
   MD5_Final(res, &ctx);
   return res;
}

void DatabaseConnectionManager::init_queries() {
   sem_init(&pu_sem, 0, 1);
   PGresult *res = PQprepare(dbConn, "postUpdate", 
                       "insert into updates (username,pid,cmd,json) values ($1,$2,$3,$4) returning updateid;",
                       0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "postUpdate: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&ap_sem, 0, 1);
   res = PQprepare(dbConn, "addProject", 
                   "insert into projects (hash,gpid,description,owner,pub,sub,protocol) values ($1,$2,$3,$4,$5,$6,$7) returning pid;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "addProject: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&aps_sem, 0, 1);
   res = PQprepare(dbConn, "addProjectSnap", 
                   "insert into projects (hash,gpid,description,owner,snapupdateid,protocol) values ($1,$2,$3,$4,$5,$6) returning pid;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "addProjectSnap: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&apf_sem, 0, 1);
   res = PQprepare(dbConn, "addProjectFork", 
                   "insert into forklist (child,parent) values ($1,$2) returning fid;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "addProjectFork: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&fpbh_sem, 0, 1);
   res = PQprepare(dbConn, "findProjectsByHash", 
                   "select p.pid,p.hash,p.gpid,p.description,f.parent,p.snapupdateid,q.description,p.pub,p.sub,p.owner,p.protocol from projects p left join (forklist f left join projects q on f.parent=q.pid) on p.pid = f.child where p.hash = $1 order by p.pid asc;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "findProjectsByHash: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&fpbp_sem, 0, 1);
   res = PQprepare(dbConn, "findProjectByPid", 
                   "select p.pid,p.hash,p.gpid,p.snapupdateid,p.description,f.parent,q.description,p.pub,p.sub,p.owner,p.protocol from projects p left join (forklist f left join projects q on f.parent=q.pid) on p.pid=f.child where p.pid = $1 order by p.pid asc;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "findProjectByPid: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&fpbg_sem, 0, 1);
   res = PQprepare(dbConn, "findProjectByGpid", 
                   "select pid,hash,gpid,protocol from projects where gpid = $1 order by pid asc;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "findProjectByGpid: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&gui_sem, 0, 1);
   res = PQprepare(dbConn, "getUserInfo", 
                   "select userid,pwhash,pub,sub from users where username = $1 order by userid asc;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "getUserInfo: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&glu_sem, 0, 1);
   res = PQprepare(dbConn, "getLatestUpdates", 
                   "select updateid,cmd,json from updates where updateid > $1 and pid = $2 order by updateid asc;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "getLatestUpdates: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&cu_sem, 0, 1);
   res = PQprepare(dbConn, "copyUpdates", 
                   "select copy_updates($1, $2, $3);",
//                   "begin; create temporary table tmptable (like updates) on commit drop; insert into tmptable select * from updates where pid = $1 and updateid <= $2; update only tmptable set pid=$3; insert into updates (select * from tmptable); commit;",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "copyUpdates: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
   sem_init(&ppu_sem, 0, 1);
   res = PQprepare(dbConn, "projectPermsUpdate", 
                   "update projects set pub=$1,sub=$2 where pid=$3",
                   0, NULL);
   if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "projectPermsUpdate: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(res);
}

DatabaseConnectionManager::DatabaseConnectionManager(json_object *conf) : ConnectionManagerBase(conf, false) {
//   if (dbConn) return;
   map<string,string> dbkeys;
   
   string dbHost = getStringOption(conf, "DB_HOST", "");
   if (dbHost.length() > 0) {
      dbkeys["hostaddr"] = dbHost; 
   }
   string dbName = getStringOption(conf, "DB_NAME", "");
   if (dbName.length() > 0) {
      dbkeys["dbname"] = dbName; 
   }
   string dbUser = getStringOption(conf, "DB_USER", "");
   if (dbUser.length() > 0) {
      dbkeys["user"] = dbUser; 
   }
   string dbPass = getStringOption(conf, "DB_PASS", "");
   if (dbPass.length() > 0) {
      dbkeys["password"] = dbPass; 
   }
   
   char const **keywords = new char const *[dbkeys.size() + 1];
   char const **values = new char const *[dbkeys.size() + 1];
   int idx = 0;
   for (map<string,string>::iterator i = dbkeys.begin(); i != dbkeys.end(); i++, idx++) {
      fprintf(stderr, "%s:%s\n", (*i).first.c_str(), (*i).second.c_str());
      keywords[idx] = (*i).first.c_str();
      values[idx] = (*i).second.c_str();
      fprintf(stderr, "%s:%s\n", (*i).first.c_str(), (*i).second.c_str());
   }
   keywords[idx] = values[idx] = NULL;
   dbConn = PQconnectdbParams(keywords, values, 0);
//   memset(dbPass, 0, strlen(dbPass));

   /* Check to see that the backend connection was successfully made */
   if (PQstatus(dbConn) != CONNECTION_OK) {
      fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(dbConn));
      PQfinish(dbConn);
   }
   else {
      init_queries();
   }
   delete [] keywords;
   delete [] values;
}

DatabaseConnectionManager::~DatabaseConnectionManager() {
   PGresult *res = PQexec(dbConn, "DEALLOCATE postUpdate;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE postUpdate;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE addProject;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE addProjectSnap;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE addProjectFork;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE findProjectsByHash;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE findProjectByPid;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE findProjectByGpid;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE getUserInfo;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE getLatestUpdates;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE copyUpdates;");
   PQclear(res);
   res = PQexec(dbConn, "DEALLOCATE projectPermsUpdate;");
   PQclear(res);
   PQfinish(dbConn);
   dbConn = NULL;
}

/**
 * authenticate authenticates a user (for use in database mode)
 * bacially this is standard CHAP with HMAC (md5)
 * @param user the user to authenticate
 * @param challenge the randomly generated challenge send to the plugin
 * @param response the calculated response from the plugin to check 
 * @return the user id of an authenticated user, or INVALID_USER
 */
int DatabaseConnectionManager::authenticate(Client *c, const char *user, const uint8_t *challenge, uint32_t clen, const uint8_t *response, uint32_t rlen) {
   int userid = -1; //INVALID_USER;
   static const int plens[1] = {0};
   static const int pformats[1] = {0};
   //insert into files values(stream_id, fname);
   const char * const parms[1] = {user};

   sem_wait(&gui_sem);
   PGresult *rset = PQexecPrepared(dbConn, "getUserInfo",
                       1, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&gui_sem);

   ExecStatusType qres = PQresultStatus(rset);
   if (qres != PGRES_TUPLES_OK || PQntuples(rset) != 1) {
      fprintf(stderr, "authenticate: %s (%s), %d\n", PQerrorMessage(dbConn), user, qres);
   }
   else {
//         fprintf(stderr, "authenticate: good add file for %d\n", htonl(id));
      //userid,pwhash,pub,sub
      uint32_t uid = ntohl(*(uint32_t*)PQgetvalue(rset, 0, 0));
      char *pwhash = PQgetvalue(rset, 0, 1);
      uint8_t *key = toByteArray(pwhash);
      int hlen = PQgetlength(rset, 0, 1);
      uint8_t *hmac = HmacMD5(challenge, clen, key, hlen / 2);
      delete [] key;
#ifdef DEBUG
      fprintf(stderr, "Trying to authenticate uid: %d, pwhash %s, hashlen: %d\n", uid, pwhash, hlen);
      fprintf(stderr, "   challenge: %s, hmac: %s\n", toHexString(challenge, clen).c_str(), toHexString(hmac, 16).c_str());
      fprintf(stderr, "    response: %s, rlen: %d\n", toHexString(response, 16).c_str(), rlen);
#endif

      if (response != NULL && memcmp(response, hmac, 16) == 0) {
         userid = uid;
         //neet to reverse results here ??
         c->setUserPub(ntohll(*(uint64_t*)PQgetvalue(rset, 0, 2)));
         c->setUserSub(ntohll(*(uint64_t*)PQgetvalue(rset, 0, 3)));
      }
      else {
#ifdef DEBUG
         fprintf(stderr, "authenticate failure\n");
#endif
         userid = -1; //INVALID_USER;
      }
      delete [] hmac;
   }
   PQclear(rset);
   return userid;
}

/**
 * migrateUpdate is very similar to 'post', migrateUpdate only 
 * archives the udpate in the database so that future clients can receive it 
 * @param newowner the new uid to attribute the update to
 * @param pid the local project id for the migrated project
 * @param cmd the 'command' that was performed (comment, rename, etc)
 * @param data the 'data' portion of the command (the comment text, etc)
 */
void DatabaseConnectionManager::migrateUpdate(const char *newowner, int pid, const char *cmd, json_object *obj) {
   logln("in migrateUpdate", LINFO4);
   uint64_t updateid = 0;

   const int plens[4] = {0, 4, 0, 0};
   static const int pformats[4] = {0, 1, 0, 0};

   pid = htonl(pid);

   size_t jlen;
   const char *jstr = json_object_to_json_string_length(obj, JSON_C_TO_STRING_PLAIN, &jlen);
   const char * const parms[4] = {newowner, (char*)&pid, cmd, jstr};

   sem_wait(&pu_sem);
   PGresult *rset = PQexecPrepared(dbConn, "postUpdate",
                       4, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&pu_sem);
   ExecStatusType qres = PQresultStatus(rset);
   if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
      fprintf(stderr, "postUpdate: %s\n", PQerrorMessage(dbConn));
   }
   else {
      updateid = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 0));
//      logln("migrated update: " + updateid + "cmd: " + cmd + "pid: " + pid + " size: " + dlen, LINFO4);
   }
   PQclear(rset);
}

/**
 * post both queues a newly received update to be sent to other clients and (if in DB mode)
 * archives the udpate in the database so that future clients can receive it 
 * @param src the client that made the update
 * @param cmd the 'command' that was performed (comment, rename, etc)
 * @param data the 'data' portion of the command (the comment text, etc)
            note that this data array already has 8 bytes (8-15) reserved to receive the updateid
            when updates are requested in the future
 */
void DatabaseConnectionManager::post(Client *c, const char *cmd, json_object *obj) {
   uint64_t updateid = 0;
   //db insert
   const int plens[4] = {0, 4, 0, 0};
   static const int pformats[4] = {0, 1, 0, 0};

   int pid = htonl(c->getPid());

   size_t jlen;
   const char *jstr = json_object_to_json_string_length(obj, JSON_C_TO_STRING_PLAIN, &jlen);
   
   const char * const parms[4] = {c->getUser().c_str(), (char*)&pid, cmd, jstr};

   sem_wait(&pu_sem);
   PGresult *rset = PQexecPrepared(dbConn, "postUpdate",
                       4, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&pu_sem);
   ExecStatusType qres = PQresultStatus(rset);
   if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
      fprintf(stderr, "postUpdate: %s\n", PQerrorMessage(dbConn));
   }
   else {
      //postgres integers are big endian so swap if necessary
      updateid = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 0));
//      fprintf(stderr, "Added update: %lld\n", updateid);
//      fprintf(stderr, "Added update: %lld, cmd: %d, pid: %d, size: %d\n", updateid, cmd, pid, dlen);
//      logln("Added update: " + updateid + ", cmd: " + cmd + ", pid: " + pid + ", size: " + data.length, LINFO4);
      sem_wait(&queueMutex);
      queue.push_back(new Packet(c, cmd, obj, updateid));   //add a new packet with the binary data to the queue
      sem_post(&queueMutex);
   }
   PQclear(rset);

   sem_post(&queueSem);
}

/**
 * sendLatestUpdates sends updates from LastUpdate to current 
 * it is expected that the client has already joined a project before calling this function
 * it is expected that the client has already received updates from 0 - lastUpdate 
 * this function is typically called when a user is re-joining a project that they had previously worked on
 * @param c the client requesting updates 
 * @param lastUpdate the last update the client received 
 */
void DatabaseConnectionManager::sendLatestUpdates(Client *c, uint64_t lastUpdate) {
   static const int plens[2] = {8, 4};
   static const int pformats[2] = {1, 1};

   int pid = htonl(c->getPid());
   
   lastUpdate = htonll(lastUpdate);
   const char * const parms[2] = {(char*)&lastUpdate, (char*)&pid};

   sem_wait(&glu_sem);
   PGresult *rset = PQexecPrepared(dbConn, "getLatestUpdates",
                       2, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&glu_sem);
   ExecStatusType qres = PQresultStatus(rset);
   if (qres != PGRES_TUPLES_OK) {
      fprintf(stderr, "getLatestUpdates: %s\n", PQerrorMessage(dbConn));
   }
   else {
      int rows = PQntuples(rset);
      for (int i = 0; i < rows; i++) {
         //integer values coming from database are big endian so swap if neccessary
         uint64_t updateid = *(uint64_t*)PQgetvalue(rset, i, 0);
         updateid = ntohll(updateid);
         const char *cmd = (const char*)PQgetvalue(rset, i, 1);
         const char *json = (const char*)PQgetvalue(rset, i, 2);
         json_object *obj = json_tokener_parse(json);

         int dlen = PQgetlength(rset, i, 2);

//         fprintf(stderr, "posting %lld (cmd %d)\n", ntohll(updateid), cmd);
//         logln("posting " + updateid + " (cmd " + cmd + ")");

         json_object_object_del(obj, "updateid");  //make sure key doesn't exist from old update
         append_json_uint64_val(obj, "updateid", updateid);
         c->post(cmd, obj);
      }
   }
   PQclear(rset);

}

/**
 * getProjectInfo gets informatio related to a local project
 * @param pid the local pid of a project to get info on
 * @return a  project info object for the provided pid
 */
ProjectInfo *DatabaseConnectionManager::getProjectInfo(int pid) {
   ProjectInfo *pinfo = NULL;

   static const int plens[1] = {4};
   static const int pformats[1] = {1};

   pid = htonl(pid);
   const char * const parms[1] = {(char*)&pid};

   sem_wait(&fpbp_sem);
   PGresult *rset = PQexecPrepared(dbConn, "findProjectByPid",
                       1, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&fpbp_sem);

   ExecStatusType qres = PQresultStatus(rset);
   //expecting a single row returned
   if (qres != PGRES_TUPLES_OK || PQntuples(rset) != 1) {
      fprintf(stderr, "findProjectByPid: %s\n", PQerrorMessage(dbConn));
   }
   else {
      uint32_t proto = ntohl(*(uint32_t*)PQgetvalue(rset, 0, 10));
      if (proto == PROTOCOL_VERSION) {
         uint32_t lpid = ntohl(*(uint32_t*)PQgetvalue(rset, 0, 0));
         char *desc = PQgetvalue(rset, 0, 4);
         int32_t parent = -1;
         if (!PQgetisnull(rset, 0, 5)) {
            parent = ntohl(*(int32_t*)PQgetvalue(rset, 0, 5));
         }
         uint64_t snapupdateid = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 3));
         const char *pdesc = "";
         if (!PQgetisnull(rset, 0, 6)) {
            pdesc = PQgetvalue(rset, 0, 6);
         }
         pinfo = new ProjectInfo(lpid, desc);
         pinfo->parent = parent;
         pinfo->pdesc = pdesc;
         pinfo->snapupdateid = snapupdateid;
         pinfo->pub = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 7));
         pinfo->sub = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 8));
         pinfo->owner = PQgetvalue(rset, 0, 9);
         pinfo->proto = proto;
         ClientSet *cs = projects.get(lpid);
         if (cs != NULL) {
            pinfo->connected = cs->size();
         }
      }
   }
   PQclear(rset);
   return pinfo;
}

/**
 * getProjectList generates a list of projects on this server, each list (vector) item is 
 * actually a pinfo (project info) object, the list does NOT contain all projects, but
 * only contains projects relevant to the binary that is currently loaded in IDA
 * @param phash the IDA generated hash that is unique among the analysis files
 * @return a vector of project info objects for the provided phash
 */
vector<ProjectInfo*> *DatabaseConnectionManager::getProjectList(const string &phash) {
   vector<ProjectInfo*> *plist = new vector<ProjectInfo*>;

   static const int plens[1] = {0};
   static const int pformats[1] = {0};

   const char * const parms[1] = {phash.c_str()};

   sem_wait(&fpbh_sem);
   PGresult *rset = PQexecPrepared(dbConn, "findProjectsByHash",
                       1, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&fpbh_sem);

   ExecStatusType qres = PQresultStatus(rset);
   if (qres != PGRES_TUPLES_OK) {
      fprintf(stderr, "findProjectsByHash: %s\n", PQerrorMessage(dbConn));
   }
   else {
      int rows = PQntuples(rset);
      for (int i = 0; i < rows; i++) {
         uint32_t proto = ntohl(*(uint32_t*)PQgetvalue(rset, i, 10));
         if (proto != PROTOCOL_VERSION) {
            continue;
         }
         uint32_t lpid = ntohl(*(uint32_t*)PQgetvalue(rset, i, 0));
         char *desc = PQgetvalue(rset, i, 3);
         int32_t parent = -1;
         if (!PQgetisnull(rset, i, 4)) {
            parent = ntohl(*(int32_t*)PQgetvalue(rset, i, 4));
         }
         uint64_t snapupdateid = ntohll(*(uint64_t*)PQgetvalue(rset, i, 5));
         const char *pdesc = "";
         if (!PQgetisnull(rset, i, 6)) {
            pdesc = PQgetvalue(rset, i, 6);
         }
         ProjectInfo *pinfo = new ProjectInfo(lpid, desc);
         pinfo->parent = parent;
         pinfo->pdesc = pdesc;
         pinfo->snapupdateid = snapupdateid;
         pinfo->pub = ntohll(*(uint64_t*)PQgetvalue(rset, i, 7));
         pinfo->sub = ntohll(*(uint64_t*)PQgetvalue(rset, i, 8));
         pinfo->owner = PQgetvalue(rset, i, 9);
         pinfo->proto = proto;
         ClientSet *cs = projects.get(lpid);
         if (cs != NULL) {
            pinfo->connected = cs->size();
         }

         plist->push_back(pinfo);
         
      }
   }
   PQclear(rset);

   return plist;
}

/**
 * joinProject joings a particular client to a project so that it can participate in collabREation 
 * @param c the client attempting to join 
 * @param lpid the local project id of the project on this server 
 * @return 0 on success, negative value on failure
 */
int DatabaseConnectionManager::joinProject(Client *c, int lpid) {
   int rval = -1;

   bool foundPid = false;

   int tpid = htonl(lpid);
   static const int plens[1] = {sizeof(tpid)};
   static const int pformats[1] = {1};

   const char * const parms[1] = {(char*)&tpid};

#ifdef DEBUG
   fprintf(stderr, "trying to join project %d\n", lpid);
#endif
   sem_wait(&fpbp_sem);
   PGresult *rset = PQexecPrepared(dbConn, "findProjectByPid",
                       1, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&fpbp_sem);

   ExecStatusType qres = PQresultStatus(rset);
   //expecting a single row returned
   if (qres != PGRES_TUPLES_OK || PQntuples(rset) != 1) {
      fprintf(stderr, "findProjectByPid: %s\n", PQerrorMessage(dbConn));
   }
   else {
      uint32_t proto = ntohl(*(uint32_t*)PQgetvalue(rset, 0, 10));
      if (proto == PROTOCOL_VERSION) {     
         const char *hash = PQgetvalue(rset, 0, 1);
   
         uint64_t snapupdateid = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 3));
   //      logln("in joinProject: " + lpid + " " + hash + " " + snapupdateid + " " + rs.getString(5) + " " + rs.getString(7), LDEBUG);
         if (snapupdateid > 0) {  //pid is a snapshot pid
            //this should now be an error condition
            
            //logln("Attempt to join snapshot " + lpid + " forking instead");
            //return forkProject(c, rs.getLong(4), rs.getString(7) + " + " + rs.getString(5));
            c->send_error("can't join a snapshot, you MUST fork a snapshot");
            logln("attempted to join a snapshop instead of forking", LERROR);
            return -1;
         }
         c->setPid(lpid);
         c->setHash(hash);
   
         const char *gpid = PQgetvalue(rset, 0, 2);
         c->setGpid(gpid);
   
         const char *owner = PQgetvalue(rset, 0, 9);
   
         if (c->getUser() == owner) { //project owner gets full perms, regardless of user, project, or requested perms
            logln("Project Owner joined! yay!", LINFO3);
            c->setPub(FULL_PERMISSIONS);
            c->setSub(FULL_PERMISSIONS);
         }
         else { //effective permissions are user perms ANDed with project perms ANDed with the perms requested by the user
            uint64_t pub = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 7));
   /*
            logln("effective publish  : " + 
                  Long.toHexString(pub)) + " & " + 
                  Long.toHexString(c->getReqPub()) + " & " + 
                  Long.toHexString(c->getUserPub()) + " = " + 
                  Long.toHexString(pub & c->getUserPub() & c->getReqPub()),LINFO1);
   */
            uint64_t sub = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 8));
   /*
            logln("effective subscribe: " + 
                  Long.toHexString(sub) + " & " + 
                  Long.toHexString(c->getReqSub()) + " & " + 
                  Long.toHexString(c->getUserSub()) + " = " + 
                  Long.toHexString(sub & c->getUserSub() & c->getReqSub()),LINFO1);
   */
            c->setPub(pub & c->getUserPub() & c->getReqPub());
            c->setSub(sub & c->getUserSub() & c->getReqSub());
         }
   
         foundPid = true;
      }
   }
   PQclear(rset);

   if (foundPid) {
      projects.addClient(c);
      rval = 0;
   }
   else {
//      logln("ERROR: attempt to join a non-existant project: " + lpid, LERROR);
   }

   return rval;
}

/**
 * snapProject adds a snapshop for a project, this does not change the client's 
 * current project, nor copy any updates, it simply marks a point-in-time (updateid wise)
 * this point-in-time can later be used as a project fork point if desired 
 * @param c the client invoking the snapshot
 * @param lastupdateid the point-in-time the client wishes to save in the snapshot
 * @param desc a user provided description of the snapshot
 * @return the snapshotid on success, -1 on failure
 */
int DatabaseConnectionManager::snapProject(Client *c, uint64_t lastupdateid, const string &desc) {
   int spid = -1;
   int oldpid = htonl(c->getPid());
   string gpid;

//   logln("User " + uid + " adding snapshot for " + c->getHash(), LINFO);
   while (true) {
      //generate a new GPID; We optimistically insert, assuming
      //this gpid is unique, and catch the SQLException if the
      //gpid uniqueness constraint is violated
      uint8_t gpid_bytes[32];
      fill_random(gpid_bytes, sizeof(gpid_bytes));
      gpid = toHexString(gpid_bytes, sizeof(gpid_bytes));
//      logln(" ... with gpid: " + gpid, LINFO2);

      const int plens[6] = {0, 0, 0, 0, 8, 4};
      static const int pformats[6] = {0, 0, 0, 0, 1, 1};
   
      int proto = htonl(PROTOCOL_VERSION);
      lastupdateid = htonll(lastupdateid);
      const char * const parms[6] = {c->getHash().c_str(), gpid.c_str(),
                                     desc.c_str(), c->getUser().c_str(), (char*)&lastupdateid, (char*)&proto};
   
      sem_wait(&aps_sem);
      PGresult *rset = PQexecPrepared(dbConn, "addProjectSnap",
                          6, //int nParams,   size of arrays that follow
                          parms, //parms,  //const char * const *paramValues, array of string values
                          plens, //const int *paramLengths,
                          pformats, //const int *paramFormats,
                          1); //int resultFormat); 0 == text, 1 == binary
      sem_post(&aps_sem);

      ExecStatusType qres = PQresultStatus(rset);
      if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
         fprintf(stderr, "addProjectSnap: %s\n", PQerrorMessage(dbConn));
      }
      else {
         spid = *(int*)PQgetvalue(rset, 0, 0);  //leave in network byte order for now
         PQclear(rset);
         break;
      }
      PQclear(rset);
   }

   static const int plens[2] = {4, 4};
   static const int pformats[2] = {1, 1};

   const char * const parms[2] = {(char*)&spid, (char*)&oldpid};

   sem_wait(&apf_sem);
   PGresult *rset = PQexecPrepared(dbConn, "addProjectFork",
                       2, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&apf_sem);

   ExecStatusType qres = PQresultStatus(rset);
   if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
      fprintf(stderr, "addProjectFork: %s\n", PQerrorMessage(dbConn));
   }
   else {
      int fid = ntohl(*(int*)PQgetvalue(rset, 0, 0));
      if (fid >= 0) {
//         logln("Snapshot id for project " + oldpid + " at updateid " + lastupdateid + " is: " + spid, LINFO);
      }
      else {
         logln("project snap failed forklist insert", LERROR);
      }
   }
   PQclear(rset);
   return ntohl(spid);
}


/**
 * forkProject  forks a project - creats new project and copies all updates to point to the new project,
 * publish and subscribe values are inherited
 * @param c client object invoking the fork
 * @param lastupdateid the updateid value the fork is to occur at
 * @param desc user provided description of the fork
 * @return the new projectid on success, -1 on failure
 */

int DatabaseConnectionManager::forkProject(Client *c, uint64_t lastupdateid, const string &desc) {
   int rval = -1;

   static const int plens[1] = {4};
   static const int pformats[1] = {1};

   int pid = htonl(c->getPid());
   const char * const parms[1] = {(char*)&pid};

   sem_wait(&fpbp_sem);
   PGresult *rset = PQexecPrepared(dbConn, "findProjectByPid",
                       1, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&fpbp_sem);

   ExecStatusType qres = PQresultStatus(rset);
   //expecting a single row returned
   if (qres != PGRES_TUPLES_OK || PQntuples(rset) != 1) {
      fprintf(stderr, "findProjectByPid: %s\n", PQerrorMessage(dbConn));
   }
   else {
      uint64_t pub = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 7));
      uint64_t sub = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 8));

//      logln("forking " + pid + " pub is " + pub + " sub is " + sub);
      rval = forkProject(c, lastupdateid, desc, pub, sub); 
   }
   PQclear(rset);

   return rval;
}


/**
 * forkProject  forks a project - creats new project and copies all updates to point to the new project
 * @param c client object invoking the fork
 * @param lastupdateid the updateid value the fork is to occur at
 * @param desc user provided description of the fork
 * @param pub specified publish permissions
 * @param sub specified subscribe permissions
 * @return the new projectid on success, -1 on failure
 */
int DatabaseConnectionManager::forkProject(Client *c, uint64_t lastupdateid, const string &desc, uint64_t pub, uint64_t sub) {
   logln("in forkProject ", LDEBUG);
   int rval = -1;

   int oldlpid = c->getPid();
   int told = htonl(oldlpid);
   remove(c);
   int lpid = addProject(c, c->getHash(), desc, pub, sub);  //could add "forked from" to desc at this point
   if (lpid >= 0) {
      //gpid and lpid are set in addProject
      //add to forklist

      static const int plens[2] = {4, 4};
      static const int pformats[2] = {1, 1};
   
      int tlpid = htonl(lpid);
      const char * const parms[2] = {(char*)&tlpid, (char*)&told};
   
      sem_wait(&apf_sem);
      PGresult *rset = PQexecPrepared(dbConn, "addProjectFork",
                          2, //int nParams,   size of arrays that follow
                          parms, //parms,  //const char * const *paramValues, array of string values
                          plens, //const int *paramLengths,
                          pformats, //const int *paramFormats,
                          1); //int resultFormat); 0 == text, 1 == binary
      sem_post(&apf_sem);
   
      ExecStatusType qres = PQresultStatus(rset);
      if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
         fprintf(stderr, "addProjectFork: %s\n", PQerrorMessage(dbConn));
      }
      else {
         int fid  = ntohl(*(int*)PQgetvalue(rset, 0, 0));    
//         logln("Forked (" + fid + "): Project " + lpid + " forked from " + oldlpid, LINFO);
      }
      PQclear(rset);

      //dup update records
      static const int plens2[3] = {4, 8, 4};
      static const int pformats2[3] = {1, 1, 1};
   
      uint64_t last = htonll(lastupdateid);
      const char * const parms2[3] = {(char*)&told, (char*)&last, (char*)&tlpid};
   
      sem_wait(&cu_sem);
      rset = PQexecPrepared(dbConn, "copyUpdates",
                          3, //int nParams,   size of arrays that follow
                          parms, //parms,  //const char * const *paramValues, array of string values
                          plens, //const int *paramLengths,
                          pformats, //const int *paramFormats,
                          1); //int resultFormat); 0 == text, 1 == binary
      sem_post(&cu_sem);
   
      qres = PQresultStatus(rset);
      if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
         fprintf(stderr, "copyUpdates: %s\n", PQerrorMessage(dbConn));
      }
      else {
//         uint64_t lastinserted = *(uint64_t*)PQgetvalue(rset, 0, 0); 
//         logln("Last inserted was " + lastinserted + ", lastupdateid was " + lastupdateid, LINFO);
         rval = lpid;
      }
      PQclear(rset);

      //at this point the project has forked and the plugin that forked is on the new project
      
      //allow anyone else on the project (w/ exactly the same updates) to follow the fork
      string gpid = lpid2gpid(lpid);
      logln("sending fork follows", LINFO);
      sendForkFollows(c, oldlpid, lastupdateid, desc);
   }
   else {
      //rejoin original project
      joinProject(c, oldlpid);
      //send fork error
      c->send_error("Fork Failed, could not create forked project");
   }
   return rval;
}

struct ForkArgs {
   Client *org;
   uint64_t lastupdate;
   const string &desc;
};

static bool offerFork(Client *c, void *user) {
   ForkArgs *fa = (ForkArgs*)user;
   if (c != fa->org) {  //sanity check, originator shouldn't be in vector anymore
//            logln("  sending follow to " + c->getUser(), LINFO3);
      c->sendForkFollow(fa->org->getUser(), fa->org->getGpid(), fa->lastupdate, fa->desc);
   }
   return true;
}   

/**
 * sendForkFollows sends a special "follow fork" message to all clients working on
 * a project that has been forked, this allows the user to decide if they would like
 * to continue to work on the existing project, or change to the newly created project
 * @param originator the client that instigated the fork
 * @param oldlpid the local pid of the original project
 * @param lastupdateid the last update processed prior to fork (if your database is different you can't change to the new project)
 * @param desc the description of the new project, so the user can make a more educated descision
 */
void DatabaseConnectionManager::sendForkFollows(Client *originator, int oldlpid, uint64_t lastupdateid, const string &desc) {
   ::logln("in sendForkFollows");
//   logln("pid " + oldlpid, LINFO3);
   ForkArgs fa = {originator, lastupdateid, desc};
   projects.loopProject(oldlpid, offerFork, &fa);
}


/**
 * snapforkProject -  this is a special version of forkProject that is designed to work
 * on snapshots (instead of existing projects) this works exactly like forkProject, execpt
 * updates are copied from the 'parent' of the snapshot instead of the client's currently 
 * associated project, also updates are copied until the lastupdateid from the snapshot, 
 * not from the plugin (last received update is stored in the idb)
 * @param c client invoking the snapforkProject
 * @param spid the pid of the project that is being snapshotted
 * @param desc the user provided description for the snapshot
 * @return the new project id on success, -1 on failure
 */

int DatabaseConnectionManager::snapforkProject(Client *c, int spid, const string &desc, uint64_t pub, uint64_t sub) {
   int rval = -1;

   int oldlpid = htonl(spid);
   
   //get lastupdateid from snapshot record
   uint64_t lastupdateid = -1;
   int parentlpid = -1;
   
   static const int plens[1] = {4};
   static const int pformats[1] = {1};

   const char * const parms[1] = {(char*)&oldlpid};

   sem_wait(&fpbp_sem);
   PGresult *rset = PQexecPrepared(dbConn, "findProjectByPid",
                       1, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&fpbp_sem);

   ExecStatusType qres = PQresultStatus(rset);
   //expecting a single row returned
   if (qres != PGRES_TUPLES_OK || PQntuples(rset) != 1) {
      fprintf(stderr, "findProjectByPid: %s\n", PQerrorMessage(dbConn));
   }
   else {
      if (!PQgetisnull(rset, 0, 5)) {
         parentlpid = ntohl(*(int*)PQgetvalue(rset, 0, 5));
      }
      lastupdateid = ntohll(*(uint64_t*)PQgetvalue(rset, 0, 3));
   }
   PQclear(rset);
   
   if (lastupdateid >= 0 && parentlpid >= 0 ) {
      int lpid = addProject(c, c->getHash(), desc, pub, sub);  
      if (lpid >= 0) {
         //gpid and lpid are set in addProject
         //add to forklist

         static const int plens[2] = {4, 4};
         static const int pformats[2] = {1, 1};
      
         int tlpid = htonl(lpid);
         const char * const parms[2] = {(char*)&tlpid, (char*)&oldlpid};
      
         sem_wait(&apf_sem);
         PGresult *rset = PQexecPrepared(dbConn, "addProjectFork",
                             2, //int nParams,   size of arrays that follow
                             parms, //parms,  //const char * const *paramValues, array of string values
                             plens, //const int *paramLengths,
                             pformats, //const int *paramFormats,
                             1); //int resultFormat); 0 == text, 1 == binary
         sem_post(&apf_sem);
      
         ExecStatusType qres = PQresultStatus(rset);
         if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
            fprintf(stderr, "addProjectFork: %s\n", PQerrorMessage(dbConn));
         }
         else {
            int fid = ntohl(*(int*)PQgetvalue(rset, 0, 0));
//            logln("Forked (" + fid + "): Project " + lpid + " forked from snapshot " + oldlpid + "(original project " + parentlpid + ")", LINFO);
         }
         PQclear(rset);
   
         //dup update records
         static const int plens2[3] = {4, 8, 4};
         static const int pformats2[3] = {1, 1, 1};
         
         parentlpid = htonl(parentlpid);
         lastupdateid = htonll(lastupdateid);
         const char * const parms2[3] = {(char*)&parentlpid, (char*)&lastupdateid, (char*)&tlpid};
      
         sem_wait(&cu_sem);
         rset = PQexecPrepared(dbConn, "copyUpdates",
                             3, //int nParams,   size of arrays that follow
                             parms, //parms,  //const char * const *paramValues, array of string values
                             plens, //const int *paramLengths,
                             pformats, //const int *paramFormats,
                             1); //int resultFormat); 0 == text, 1 == binary
         sem_post(&cu_sem);
      
         qres = PQresultStatus(rset);
         if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
            fprintf(stderr, "copyUpdates: %s\n", PQerrorMessage(dbConn));
         }
         else {
//            uint64_t lastinserted = *(uint64_t*)PQgetvalue(rset, 0, 0); 
//            logln("Last inserted was " + lastinserted + ", lastupdateid was " + lastupdateid, LINFO);
            rval = lpid;
         }
         PQclear(rset);
      }
      else {
         c->send_error("attempt to snapfork a project (not a snapshot)");
      }
   }
   return rval;
}

/**
 * migrateProject adds a project to the database  
 * fairly similar to addProject
 * @param owner the uid to be the owner of the new project
 * @param gpid unique global id for the incoming project
 * @param hash unique hash for the binary file originally generated by IDA
 * @param desc user provided description of the project 
 * @param pub the publish permissions for the project
 * @param sub the subscribe permissions for the project
 * @return the new project id on success, -1 on failure
 */

int DatabaseConnectionManager::migrateProject(const char *owner, const string &gpid, const string &hash, const string &desc, uint64_t pub, uint64_t sub) {
   logln("in migrateProject ", LDEBUG);
   int lpid = -1;

//   logln("Owner " + owner + " migrating project for " + hash, LINFO);
//   logln(" P " + pub + "   S " + sub, LINFO);
//   logln(" ... with gpid: " + gpid, LINFO1);

   const int plens[7] = {0, 0, 0, 0, 8, 8, 4};
   static const int pformats[7] = {0, 0, 0, 0, 1, 1, 1};

   int proto = htonl(PROTOCOL_VERSION);
   const char * const parms[7] = {hash.c_str(), gpid.c_str(),
                                  desc.c_str(), owner, (char*)&pub, (char*)&sub, (char*)&proto};
   pub = htonll(pub);
   sub = htonll(sub);
   sem_wait(&ap_sem);
   PGresult *rset = PQexecPrepared(dbConn, "addProject",
                       7, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&ap_sem);

   ExecStatusType qres = PQresultStatus(rset);
   if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
      fprintf(stderr, "addProject: %s\n", PQerrorMessage(dbConn));
   }
   else {
      lpid = ntohl(*(int*)PQgetvalue(rset, 0, 0));
   }
   PQclear(rset);

   return lpid;
}

/**
 * addProject adds a project to the database and reflector (or merely a reflector in non-DB mode) 
 * @param c cliend invoking the addProject
 * @param hash unique hash for the binary file originally generated by IDA
 * @param desc user provided description of the project 
 * @param pub the publish permissions for the project
 * @param sub the subscribe permissions for the project
 * @return the new project id on success, -1 on failure
 */

int DatabaseConnectionManager::addProject(Client *c, const string &hash, const string &desc, uint64_t pub, uint64_t sub) {
   static const int pformats[7] = {0, 0, 0, 0, 1, 1, 1};

   logln("in addProject ", LDEBUG);
   int lpid = -1;
   string gpid;

//   logln("User " + uid + " adding project for " + hash, LINFO);
//   logln(" P " + pub + "   S " + sub, LINFO);

   pub = htonll(pub);
   sub = htonll(sub);

   int proto = htonl(PROTOCOL_VERSION);
   while (true) {
      //generate a new GPID; We optimistically insert, assuming
      //this gpid is unique, and catch the SQLException if the
      //gpid uniqueness constraint is violated
      uint8_t gpid_bytes[32];
      fill_random(gpid_bytes, sizeof(gpid_bytes));
      gpid = toHexString(gpid_bytes, sizeof(gpid_bytes));
//      logln(" ... with gpid: " + gpid, LINFO2);

      const int plens[7] = {0, 0, 0, 0, 8, 8, 4};
   
      const char * const parms[7] = {hash.c_str(), gpid.c_str(),
                                     desc.c_str(), c->getUser().c_str(), (char*)&pub, (char*)&sub, (char*)&proto};
   
      sem_wait(&ap_sem);
      PGresult *rset = PQexecPrepared(dbConn, "addProject",
                          7, //int nParams,   size of arrays that follow
                          parms, //parms,  //const char * const *paramValues, array of string values
                          plens, //const int *paramLengths,
                          pformats, //const int *paramFormats,
                          1); //int resultFormat); 0 == text, 1 == binary
      sem_post(&ap_sem);

      ExecStatusType qres = PQresultStatus(rset);
      if (qres != PGRES_TUPLES_OK && qres != PGRES_COMMAND_OK) {
         fprintf(stderr, "addProject: %s\n", PQerrorMessage(dbConn));
      }
      else {
         lpid = ntohl(*(int*)PQgetvalue(rset, 0, 0));
         c->setPid(lpid);
         c->setGpid(gpid);
         //this is a newly created project, user of c must be the owner
         c->setPub(FULL_PERMISSIONS);
         c->setSub(FULL_PERMISSIONS);
         PQclear(rset);
         break;
      }
      PQclear(rset);
   }
   if (lpid != -1) {
      projects.addClient(c);
   }
   return lpid;
}

struct UpdateArgs {
   Client *owner;
   uint64_t pub;
   uint64_t sub;
};

static bool updatePerms(Client *c, void *user) {
   UpdateArgs *args = (UpdateArgs*)user;
   Client *owner = args->owner;
   if (c != owner) {
      uint64_t oldpperm = c->getPub(); 
      uint64_t newpperm = (c->getUserPub() & c->getReqPub() & args->pub);
      uint64_t oldsperm = c->getSub(); 
      uint64_t newsperm = (c->getUserSub() & c->getReqSub() & args->sub);
      if (oldpperm != newpperm) {
/*
         logln("updating " + (*si)->getUser() + 
               " from p " + newpperm + "(was: " + oldpperm + ")" +
               " to s "   + newsperm + "(was: " + oldsperm + ")",LINFO3);
*/
         c->setPub(newpperm);
         c->setSub(newsperm);
         c->send_error("You permissions have changed as a result of the project owner changing project permissions");
      } 
   }
   return true;
}

/**
 * updateProjectPerms updates the publish and subscribe values in the database, it also iterates
 * across all clients connected the project and updates the effective permissions accordingly
 * @param pub the publish permissions to set
 * @param sub the subscribe permissions to set
 */
void DatabaseConnectionManager::updateProjectPerms(Client *c, uint64_t pub, uint64_t sub) {
   static const int plens[3] = {8, 8, 4};
   static const int pformats[3] = {1, 1, 1};

   int pid = htonl(c->getPid());
   uint64_t tpub = htonll(pub);
   uint64_t tsub = htonll(sub);
   const char * const parms[3] = {(char*)&tpub, (char*)&tsub, (char*)&pid};

//   logln("Setting project " + pid + " permissions to p " + pub + " s " + sub, LINFO2);
   sem_wait(&ppu_sem);
   PGresult *rset = PQexecPrepared(dbConn, "projectPermsUpdate",
                       3, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&ppu_sem);

   ExecStatusType qres = PQresultStatus(rset);
   if (qres != PGRES_COMMAND_OK) {
      fprintf(stderr, "projectPermsUpdate: %s\n", PQerrorMessage(dbConn));
   }
   PQclear(rset);
         
   logln("recalculating effective permissions for connected clients", LINFO3);

   UpdateArgs args = {c, pub, sub};
   projects.loopProject(c->getPid(), updatePerms, &args);
}

/**
 * gpid2lpid converts a gpid (which is unique across all projects on all servers)
 * to an lpid (pid local to a particular server instance) 
 * @param gpid global pid 
 * @return the local pid
 */
int DatabaseConnectionManager::gpid2lpid(const string &gpid) {
   int lpid = -1;
//   logln("lookup up: " + gpid, LINFO3);

   static const int plens[1] = {0};
   static const int pformats[1] = {0};

   const char * const parms[1] = {gpid.c_str()};

   sem_wait(&fpbg_sem);
   PGresult *rset = PQexecPrepared(dbConn, "findProjectByGpid",
                       1, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&fpbg_sem);

   ExecStatusType qres = PQresultStatus(rset);
   //expecting exactly 1 row
   if (qres != PGRES_TUPLES_OK || PQntuples(rset) != 1) {
      fprintf(stderr, "findProjectByGpid: %s\n", PQerrorMessage(dbConn));
   }
   else {
      lpid = ntohl(*(int*)PQgetvalue(rset, 0, 0));
//      logln("found: " + lpid, LINFO3);
   }
   PQclear(rset);

   return lpid;
}

/**
 * lpid2gpid converts an lpid (pid local to a particular server instance) 
 * to a gpid (which is unique across all projects on all servers)
 * @param lpid the local pid for this particular server 
 * @return the glocabl pid
 */
string DatabaseConnectionManager::lpid2gpid(int lpid) {
   const char *rval = "";

   static const int plens[1] = {4};
   static const int pformats[1] = {1};

   lpid = htonl(lpid);
   const char * const parms[1] = {(char*)&lpid};

   sem_wait(&fpbp_sem);
   PGresult *rset = PQexecPrepared(dbConn, "findProjectByPid",
                       1, //int nParams,   size of arrays that follow
                       parms, //parms,  //const char * const *paramValues, array of string values
                       plens, //const int *paramLengths,
                       pformats, //const int *paramFormats,
                       1); //int resultFormat); 0 == text, 1 == binary
   sem_post(&fpbp_sem);

   ExecStatusType qres = PQresultStatus(rset);
   //expecting exactly 1 result row
   if (qres != PGRES_TUPLES_OK || PQntuples(rset) != 1) {
      fprintf(stderr, "findProjectByPid: %s\n", PQerrorMessage(dbConn));
   }
   else {
      rval = PQgetvalue(rset, 0, 2);
   }
   PQclear(rset);

   return rval;
}
