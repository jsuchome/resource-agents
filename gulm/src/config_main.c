/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <argz.h>
#include <netdb.h>

#include "gulm_defines.h"
#include "config_priv.h"
#include "LLi.h"
#include "hash.h"
#include "config_gulm.h"
#include "utils_ip.h"
#include "utils_dir.h"
#include "utils_crc.h"
#include "utils_verb_flags.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

/*****************************************************************************/

/**
 * release_node_list - 
 * @list: <> list to free up
 *
 * free up all of the elements in a LLi of ip_name_t.
 * 
 * 
 * Returns: void
 */
void release_node_list(LLi_t *list)
{
   LLi_t *tmp, *nxt;
   ip_name_t *in;
   for(tmp = LLi_next(list);
       NULL != LLi_data(tmp);
       tmp = nxt ) {
      nxt = LLi_next(tmp);
      LLi_del(tmp);
      in = LLi_data(tmp);
      if( in->name != NULL ) free(in->name);
      free(in);
   }
}

/**
 * Can_I_be_a_master - 
 * or, am i in the servers list?
 * Returns: TRUE or FALSE
 */
int Can_I_be_a_master(gulm_config_t *gf, struct in6_addr *ip)
{
   LLi_t *tmp;
   ip_name_t *in;
   if( LLi_empty(&gf->node_list) ) return FALSE;
   for(tmp = LLi_next(&gf->node_list);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp)) {
      in = LLi_data(tmp);
      if( IN6_ARE_ADDR_EQUAL(in->ip.s6_addr32 , ip->s6_addr32) ) {
         return TRUE;
      }
   }
   return FALSE;
}

/*****************************************************************************/

/* this really needs to be done better. */
unsigned int calc_quorum(unsigned int quorum, unsigned int cnt)
{
   if( quorum > cnt ) {
      return (cnt / 2 ) + 1;
   }else if( quorum == 0 )
      return 1;
   else
      return quorum;
}


/*****************************************************************************/

/**
 * hash_config - 
 * 
 * generate a hash key for this config.  This is used to make sure all
 * servers are using compatible configs.
 *
 * Note that only the important bits are hashed.  Parts not hashed can
 * differ on nodes, and not affect the cluster's ablity to run.  fencebin
 * is an example, as is the verbosity.
 * 
 * Returns: uint32_t
 */
uint32_t hash_config(gulm_config_t *gf)
{
   register uint32_t hash = 0x6d696b65;
   LLi_t *tmp;
   ip_name_t *in;

   hash = crc32(gf->clusterID, strlen(gf->clusterID), hash);
   hash = crc32((uint8_t*)&gf->corePort, sizeof(uint16_t), hash);
   hash = crc32((uint8_t*)&gf->heartbeat_rate, sizeof(uint64_t), hash);
   hash = crc32((uint8_t*)&gf->allowed_misses, sizeof(uint16_t), hash);
   hash = crc32((uint8_t*)&gf->quorum, sizeof(uint16_t), hash);
   /* hashing fog is redundent now that it is just (node_cnt > 1) */
   hash = crc32((uint8_t*)&gf->fog, sizeof(uint16_t), hash);

   hash = crc32((uint8_t*)&gf->node_cnt, sizeof(uint16_t), hash);
   for(tmp = LLi_next(&gf->node_list);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp) ) {
      in = LLi_data(tmp);
      hash = crc32((uint8_t*)&in->ip, sizeof(uint32_t), hash);
   }

   hash = crc32((uint8_t*)&gf->how_many_lts, sizeof(uint16_t), hash);
   hash = crc32((uint8_t*)&gf->lt_port, sizeof(uint16_t), hash);
   hash = crc32((uint8_t*)&gf->ltpx_port, sizeof(uint16_t), hash);

   return hash;
}

/**
 * dump_conf - 
 *
 * This will change to match the new file format once that is decided and
 * written.
 * 
 */
void dump_conf(gulm_config_t *gf, int out)
{
   FILE *fp;
   int fd;
   LLi_t *tmp;
   ip_name_t *in;
   uint64_t pf;

   if( out ) {
      fp = stdout;
   }else{
      if( (fd=open_tmp_file("Gulm_config")) < 0 ) return;
      if((fp = fdopen(fd,"a")) == NULL ) return;
   }
   fprintf(fp,"#======================================="
         "========================================\n");

   fprintf(fp, "# hashed: %#x\n", gf->hashval);
   fprintf(fp, "/cluster/@name = \"%s\"\n", gf->clusterID);
   pf = gf->heartbeat_rate / 1000;
   fprintf(fp, "/cluster/gulm/heartbeat_rate = %.3f\n", pf / 1000.0 );
   fprintf(fp, "/cluster/gulm/allowed_misses = %d\n", gf->allowed_misses);
   fprintf(fp, "/cluster/gulm/coreport = %d\n", gf->corePort);
   pf = gf->new_con_timeout / 1000;
   fprintf(fp, "/cluster/gulm/new_connection_timeout = %.3f\n", pf / 1000.0 );
   fprintf(fp, "# quorum = %d\n", gf->quorum);
   fprintf(fp, "# server cnt: %d\n", gf->node_cnt);

   tmp = LLi_next(&gf->node_list);
   in = LLi_data(tmp);
   fprintf(fp, "# servers = %s", in->name);
   for(tmp = LLi_next(tmp);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp) ) {
      in = LLi_data(tmp);
      fprintf(fp, " %s", in->name);
   }
   fprintf(fp, "\n");
   tmp = LLi_next(&gf->node_list);
   in = LLi_data(tmp);
   fprintf(fp, "/cluster/gulm/servers = %s", ip6tostr(&in->ip));
   for(tmp = LLi_next(tmp);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp) ) {
      in = LLi_data(tmp);
      fprintf(fp, " %s", ip6tostr(&in->ip));
   }
   fprintf(fp, "\n");

   fprintf(fp, "/cluster/gulm/lt_partitions = %d\n", gf->how_many_lts);
   fprintf(fp, "/cluster/gulm/lt_base_port = %d\n", gf->lt_port);
   fprintf(fp, "/cluster/gulm/lt_high_locks = %ld\n", gf->lt_maxlocks);
   fprintf(fp, "/cluster/gulm/lt_drop_req_rate = %d\n", gf->lt_cf_rate);
   fprintf(fp, "/cluster/gulm/prealloc_locks = %d\n", gf->lt_prelocks);
   fprintf(fp, "/cluster/gulm/prealloc_holders = %d\n", gf->lt_preholds);
   fprintf(fp, "/cluster/gulm/prealloc_lkrqs = %d\n", gf->lt_prelkrqs);
   fprintf(fp, "/cluster/gulm/ltpx_port = %d\n", gf->ltpx_port);

   if( !out ) {
      fclose(fp);
   }
}

/**
 * get_lt_range - 
 * @which: 
 * @of:
 * @start: 
 * @stop: 
 * 
 * Given which lt in a set, what would it's start stop range be?
 * For example:  which:3 of:10 => start:78 stop:103
 *
 * Wonder if there is a way to do this with out the loop. probably.
 *
 * This should go into a different file. (seems only the ltpx uses it, so
 * over there somewhere.)
 * 
 */
int get_lt_range(int which, int of, int *start, int *stop)
{
   int i, eg, lo, last=0;

   if( of > 256 ) return -EINVAL;
   if( which > of ) return -EINVAL;

   eg = ( 256 - of ) / of;
   lo = ( 256 - of ) % of;

   for(i = 0 ; i < of ; i++ ) {
      *start = last;
      last += eg;
      if( lo > 0 ) { lo--; last ++; }
      *stop = last;
      last++;

      if( i == which ) break;
   }
   return 0;
}

/**
 * strdup_with_free - 
 * @dst: 
 * @src: 
 * 
 * 
 * Returns: void
 */
void strdup_with_free(char **dst, char *src)
{
   if( *dst != NULL ) free(*dst);
   *dst = strdup(src);
   if( *dst == NULL ) die(ExitGulm_NoMemory, "Out of Memory.\n");
}

/**
 * bound_uint16 - 
 * @val: 
 * @min: 
 * @max: 
 * 
 * given an int, make sure it fits within the range given my the min and
 * max.  and return the value within a uin16_t
 *
 * do the compairs on ints, but return uint16
 *
 * One of the big things I'm trying to do here, is that if given a -1,
 * return the min value.  If we convert to a unsigned first, we'll end up
 * with the max value, and that isn't what we want.
 * 
 * Returns: uint16_t
 */
uint16_t bound_to_uint16(int val, uint16_t min, uint16_t max)
{
   if( val < (int)min ) return min;
   if( val > (int)max ) return max;
   return val;
}

unsigned int bound_to_uint(int val, unsigned int min, unsigned int max)
{
   if( val < min ) return min;
   if( val > max ) return max;
   return val;
}
unsigned long bound_to_ulong(int val, unsigned long min, unsigned long max)
{
   if( val < min ) return min;
   if( val > max ) return max;
   return val;
}
uint64_t bound_to_uint64(uint64_t val, uint64_t min, uint64_t max)
{
   if( val < min ) return min;
   if( val > max ) return max;
   return val;
}

/**
 * ft2uint64 - 
 * @time: < float in form <sec>.<millisec>
 * 
 * 
 * Returns: uint64_t time in micro-seconds
 */
uint64_t ft2uint64(float time)
{
   return ((uint64_t)(time * 1000)) * 1000;
}

/**
 * uint642ft - 
 * @time: < uint64_t time in micro-seconds
 * 
 * 
 * Returns: float in form <sec>.<millisec>
 */
float uint642ft(uint64_t time)
{
   return ((float)(time/1000)) / 1000.0;
}

/**
 * serialize_config - 
 * @gf: 
 * @enc: 
 * 
 * 
 * Returns: void
 */
void serialize_config(gulm_config_t *gf, xdr_enc_t *enc)
{
   char workspace[1024];
   char *sl=NULL;
   size_t lsl=0;
   LLi_t *tmp;
   ip_name_t *in;

   xdr_enc_string(enc, "hashed");
   snprintf(workspace, 1024, "%#x", gf->hashval);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "cluster_name");
   xdr_enc_string(enc, gf->clusterID);

   xdr_enc_string(enc, "serverips");
   for(tmp = LLi_next(&gf->node_list);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp) ) {
      in = LLi_data(tmp);
      argz_add(&sl, &lsl, ip6tostr(&in->ip));
   }
   argz_stringify(sl, lsl, ',');
   xdr_enc_string(enc, sl);
   free(sl);sl=NULL;lsl=0;

   xdr_enc_string(enc, "servernamess");
   for(tmp = LLi_next(&gf->node_list);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp) ) {
      in = LLi_data(tmp);
      argz_add(&sl, &lsl, in->name);
   }
   argz_stringify(sl, lsl, ',');
   xdr_enc_string(enc, sl);
   free(sl);sl=NULL;

   xdr_enc_string(enc, "heartbeat_rate");
   snprintf(workspace, 1024, "%.3f", uint642ft(gf->heartbeat_rate));
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "allowed_misses");
   snprintf(workspace, 1024, "%u", gf->allowed_misses);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "verbosity");
   get_verbosity_string(workspace, 1024, verbosity);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "lt_partitions");
   snprintf(workspace, 1024, "%u", gf->how_many_lts);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "new_connection_timeout");
   snprintf(workspace, 1024, "%.3f", uint642ft(gf->new_con_timeout));
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "master_scan_delay");
   snprintf(workspace, 1024, "%.3f", uint642ft(gf->master_scan_delay));
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "coreport");
   snprintf(workspace, 1024, "%u", gf->corePort);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "ltport");
   snprintf(workspace, 1024, "%u", gf->lt_port);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "ltpxport");
   snprintf(workspace, 1024, "%u", gf->ltpx_port);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "fence_bin");
   xdr_enc_string(enc, gf->fencebin);

   xdr_enc_string(enc, "run_as");
   xdr_enc_string(enc, gf->run_as);

   xdr_enc_string(enc, "lock_dir");
   xdr_enc_string(enc, gf->lock_file);

   xdr_enc_string(enc, "lt_drop_req_rate");
   snprintf(workspace, 1024, "%u", gf->lt_cf_rate);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "lt_high_locks");
   snprintf(workspace, 1024, "%lu", gf->lt_maxlocks);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "prealloc_locks");
   snprintf(workspace, 1024, "%u", gf->lt_prelocks);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "prealloc_lkrqs");
   snprintf(workspace, 1024, "%u", gf->lt_prelkrqs);
   xdr_enc_string(enc, workspace);

   xdr_enc_string(enc, "prealloc_holders");
   snprintf(workspace, 1024, "%u", gf->lt_preholds);
   xdr_enc_string(enc, workspace);

}

/**
 * validate_config - 
 * @gf: 
 * 
 * 
 */
void validate_config(gulm_config_t *gf)
{
   if( gf->clusterID == NULL ) {
      fprintf(stderr, "Gulm requires a cluster name. You left it blank.\n");
      exit(ExitGulm_ParseFail);
   }
   if( !(gf->node_cnt > 0 && gf->node_cnt <= 5 && gf->node_cnt != 2) ) {
      fprintf(stderr, "Gulm requires 1,3,4, or 5 nodes to be specified in "
            "the servers list.  You specified %d\n", gf->node_cnt);
      exit(ExitGulm_ParseFail);
   }
}

/**
 * default_config - 
 * @gf: 
 * 
 * 
 */
void default_config(gulm_config_t *gf)
{
   char workspace[256];
   memset(gf, 0, sizeof(gulm_config_t));

   gf->clusterID = NULL; //strdup("cluster");
   gf->fencebin = strdup("fence_node");
   gf->run_as = strdup("root");
   gf->lock_file = strdup("/var/run/sistina");
   gf->corePort = 40040;
   gf->heartbeat_rate = ft2uint64(15.0);
   gf->master_scan_delay = ft2uint64(1.0);
   gf->new_con_timeout = ft2uint64(15.0);
   gf->allowed_misses = 2;
   
   LLi_init_head( &gf->node_list );

   gf->how_many_lts = 1;
   gf->lt_port = 41040;
   gf->ltpx_port = 40042;

   gf->lt_cf_rate = 10;
   gf->lt_maxlocks = 1024 * 1024;
   gf->lt_hashbuckets = 65536;
   gf->lt_prelocks = 10;
   gf->lt_prelkrqs = 10;
   gf->lt_preholds = 10;

   gf->conf_test = FALSE;
   gf->leave_std_open = FALSE;
   gf->daemon_fork = TRUE;
   gethostname(workspace, 256);
   gf->name = strdup(workspace);
   memcpy(&gf->ip, &in6addr_any, sizeof(struct in6_addr));
   gf->netdev = NULL;

}

/**
 * build_argv - 
 * @gf: 
 * @argv: 
 * @argc: 
 * 
 * recreates the command line args needed to run gulm
 * 
 * Returns: int
 */
int build_argv(gulm_config_t *gf, char ***argv, int *argc)
{
   char workspace[1024];
   char *argz=NULL, *sl=NULL;
   size_t laz=0, lsl=0;
   LLi_t *tmp;
   ip_name_t *in;

   argz_add(&argz, &laz, ProgramName);

   /* Always have a name and servers list. */
   argz_add(&argz, &laz, "--cluster_name");
   argz_add(&argz, &laz, gf->clusterID);

   argz_add(&argz, &laz, "--servers");
   for(tmp = LLi_next(&gf->node_list);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp) ) {
      in = LLi_data(tmp);
      argz_add(&sl, &lsl, ip6tostr(&in->ip));
   }
   argz_stringify(sl, lsl, ',');
   argz_add(&argz, &laz, sl);
   free(sl);

   /* now we get optionals */
   gethostname(workspace, 1024);
   if( strcmp(gf->name, workspace) != 0 ) {
      argz_add(&argz, &laz, "--name");
      argz_add(&argz, &laz, gf->name);
   }
   if( memcmp(&gf->ip, &in6addr_any, sizeof(struct in6_addr)) != 0 ) {
      argz_add(&argz, &laz, "--ip");
      argz_add(&argz, &laz, ip6tostr(&gf->ip));
   }
   if( gf->netdev != NULL ) {
      argz_add(&argz, &laz, "--ifdev");
      argz_add(&argz, &laz, gf->netdev);
   }
   if( gf->conf_test ) {
      argz_add(&argz, &laz, "-C");
   }
   if( gf->leave_std_open ) {
      argz_add(&argz, &laz, "-e");
   }
   if( ! gf->daemon_fork ) {
      argz_add(&argz, &laz, "-d");
   }
   if( verbosity != (lgm_Network|lgm_Stomith|lgm_Forking) ) {
      get_verbosity_string(workspace, 1024, verbosity);
      argz_add(&argz, &laz, "--verbosity");
      argz_add(&argz, &laz, workspace);
   }
   if( gf->heartbeat_rate != ft2uint64(15.0) ) {
      argz_add(&argz, &laz, "--heartbeat_rate");
      snprintf(workspace, 1024, "%.3f", uint642ft(gf->heartbeat_rate));
      argz_add(&argz, &laz, workspace);
   }
   if( gf->allowed_misses != 2 ) {
      argz_add(&argz, &laz, "--allowed_misses");
      snprintf(workspace, 1024, "%u", gf->allowed_misses);
      argz_add(&argz, &laz, workspace);
   }
   if( gf->how_many_lts != 1 ) {
      argz_add(&argz, &laz, "--lt_partitions");
      snprintf(workspace, 1024, "%u", gf->how_many_lts);
      argz_add(&argz, &laz, workspace);
   }
   if( gf->new_con_timeout != ft2uint64(15.0) ) {
      argz_add(&argz, &laz, "--new_connection_timeout");
      snprintf(workspace, 1024, "%.3f", uint642ft(gf->new_con_timeout));
      argz_add(&argz, &laz, workspace);
   }
   if( gf->master_scan_delay != ft2uint64(1.0) ) {
      argz_add(&argz, &laz, "--master_scan_delay");
      snprintf(workspace, 1024, "%.3f", uint642ft(gf->master_scan_delay));
      argz_add(&argz, &laz, workspace);
   }
   if( gf->corePort != 40040 ) {
      argz_add(&argz, &laz, "--coreport");
      snprintf(workspace, 1024, "%u", gf->corePort);
      argz_add(&argz, &laz, workspace);
   }
   if( gf->lt_port != 41040 ) {
      argz_add(&argz, &laz, "--ltport");
      snprintf(workspace, 1024, "%u", gf->lt_port);
      argz_add(&argz, &laz, workspace);
   }
   if( gf->ltpx_port != 40042 ) {
      argz_add(&argz, &laz, "--ltpxport");
      snprintf(workspace, 1024, "%u", gf->ltpx_port);
      argz_add(&argz, &laz, workspace);
   }
   if( strcmp(gf->fencebin, "fence_node") != 0 ) {
      argz_add(&argz, &laz, "--fence_bin");
      argz_add(&argz, &laz, gf->fencebin);
   }
   if( strcmp(gf->run_as, "root") != 0  ) {
      argz_add(&argz, &laz, "--run_as");
      argz_add(&argz, &laz, gf->run_as);
   }
   if( strcmp(gf->lock_file, "/var/run/sistina") != 0  ) {
      argz_add(&argz, &laz, "--lock_dir");
      argz_add(&argz, &laz, gf->lock_file);
   }
   if( gf->lt_cf_rate != 10 ) {
      argz_add(&argz, &laz, "--lt_drop_req_rate");
      snprintf(workspace, 1024, "%u", gf->lt_cf_rate);
      argz_add(&argz, &laz, workspace);
   }
   if( gf->lt_maxlocks != 1024 * 1024 ) {
      argz_add(&argz, &laz, "--lt_high_locks");
      snprintf(workspace, 1024, "%lu", gf->lt_maxlocks);
      argz_add(&argz, &laz, workspace);
   }
   if( gf->lt_prelocks != 10 ) {
      argz_add(&argz, &laz, "--prealloc_locks");
      snprintf(workspace, 1024, "%u", gf->lt_prelocks);
      argz_add(&argz, &laz, workspace);
   }
   if( gf->lt_prelkrqs != 10 ) {
      argz_add(&argz, &laz, "--prealloc_lkrqs");
      snprintf(workspace, 1024, "%u", gf->lt_prelkrqs);
      argz_add(&argz, &laz, workspace);
   }
   if( gf->lt_preholds != 10 ) {
      argz_add(&argz, &laz, "--prealloc_holders");
      snprintf(workspace, 1024, "%u", gf->lt_preholds);
      argz_add(&argz, &laz, workspace);
   }

   *argc = argz_count(argz, laz) + 1;
   *argv = malloc(sizeof(char *) * (*argc));
   argz_extract(argz, laz, *argv);
   
   return 0;
}

/**
 * parse_conf - 
 * @gf: 
 *
 * Now do heavy parsing.
 *
 * Returns: int
 */
int parse_conf(gulm_config_t *gf, int argc, char **argv)
{
   int err=0;

   /* should set defaults here. */
   default_config(gf);

   /* parse cmdline args */
   err = parse_cmdline(gf, argc, argv);

   /* ok, read from everywhere, Now make sure we have the bare minimum
    * required to run
    *
    * actually, I think the only thing this currently needs to do is to make
    * sure that there is a valid number of servers. (1,3,4,5)
    */
   validate_config(gf);

   /* Now that we know what available, do some last minute adjustments. */

   /* more than one server? then we must be foggy. */
   gf->fog = (gf->node_cnt > 1);

   /* calc quorum */
   gf->quorum = (gf->node_cnt / 2) +1;

   /* calc hash. */
   gf->hashval = hash_config(gf);

   if( gf->conf_test ) {
      dump_conf(gf, TRUE);
      exit(0);
   }

   /* all done. */
   return err;
}

/**
 * verify_name_and_ip - 
 * @name: < name that this node is claiming.
 * @ip: < ip from which this node came
 * 
 *
 * 
 * Returns: =0:Deny =1:Allow
 */
int verify_name_and_ip(char *name, struct in6_addr *ip)
{
   struct in6_addr testip;

   return 1;

   /* check with libresolv */
   if( get_ip_for_name(name, &testip) != 0 ) {
      log_msg(lgm_Network2,"Failed to lookup ip for %s\n", name);
      return 0;
   }
   if( !IN6_ARE_ADDR_EQUAL(testip.s6_addr32, ip->s6_addr32) ) {
      log_msg(lgm_Network2,"For %s, ip %s doesn't match %s\n", name,
            ip6tostr(ip), ip6tostr(&testip));
      /* XXX above print won't be right due to static bufs and inlining */
      return 0;
   }

   /* if ccs, check with them too */
   if( verify_name_and_ip_ccs(name, ip) == 0 ) {
      return 0;
   }

   return 1;
}

/* vim: set ai cin et sw=3 ts=3 : */
