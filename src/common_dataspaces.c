/*
 * Copyright (c) 2009, NSF Cloud and Autonomic Computing Center, Rutgers University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided
 * that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this list of conditions and
 * the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided with the distribution.
 * - Neither the name of the NSF Cloud and Autonomic Computing Center, Rutgers University, nor the names of its
 * contributors may be used to endorse or promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
*  Ciprian Docan (2009)  TASSL Rutgers University
*  docan@cac.rutgers.edu
*  Tong Jin (2011) TASSL Rutgers University
*  tjin@cac.rutgers.edu
*  Hoang Bui (2012-2013) TASSL Rutgers University
*  hbui@cac.rutgers.edu
*  Fan Zhang (2013) TASSL Rutgers University
*  zhangfan@cac.rutgers.edu
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#include "common_dataspaces.h"
#include "debug.h"
#include "util.h"
#include "dc_gspace.h"
#include "ss_data.h"
#include "timer.h"
#include "dataspaces.h"


#ifdef DS_HAVE_DIMES
#include "dimes_client.h"
#endif

static struct dcg_space *dcg = NULL;
static struct timer timer;
static int sync_op_id;
static int cq_id = -1; // TODO: still support it?
static enum storage_type st = column_major; // TODO: still need this?
static int num_dims = 2; // TODO: remove it 
#ifdef DS_HAVE_DIMES
static struct dimes_client *dimes_c = NULL;
#endif

static void lib_exit(void)
{
        dcg_free(dcg);
        exit(EXIT_FAILURE);
}

#define ERROR_TRACE_AND_EXIT()					\
do {								\
	uloga("'%s()': failed with %d.\n", __func__, err);	\
	lib_exit();						\
} while (0)

#ifdef DS_HAVE_DIMES
static int is_dimes_lib_init()
{
    if (!dimes_c) {
        uloga("ERROR: dimes library was not properly initialized!\n");
        return 0;
    }
    return 1;
}
#endif

static int is_dspaces_lib_init() { 
    if (!dcg) {
        uloga("ERROR: dspaces library was not properly initialized!\n");
        return 0;
    }
    return 1;
}

static int is_ndim_within_bound(int ndim) {
    if (ndim > BBOX_MAX_NDIM) {
        uloga("ERROR: maximum number of array dimension supported is %d "
            "but ndim is %d\n", BBOX_MAX_NDIM, ndim);
        return 0;
    }
    return 1;
}

/* 
   Common interface for DataSpaces.
*/
int common_dspaces_init(int num_peers, int appid, void *comm, const char *parameters)
{
	int err = -ENOMEM;

	if (dcg) {
		/* Library already initialized. */
		return 0;
	}

    /*struct name_value_pair *params, *p;
    params = text_to_nv_pairs(parameters);
    p  = params;
    while (p) {
        p = p->next;
    }*/

	dcg = dcg_alloc(num_peers, appid, comm);
	if (!dcg) {
        uloga("%s(): failed to initialize.\n", __func__);
		return err;
	}

	err = dcg_ss_info(dcg, &num_dims);
	if (err < 0) {
		uloga("%s(): failed to obtain space info.\n", __func__);
		return err;
	}

#ifdef DS_HAVE_DIMES
    dimes_c = dimes_client_alloc(dcg);
    if (dimes_c == NULL) {
        uloga("%s(): failed to init DIMES.\n", __func__);
        return err;
    }
#endif

    //free_nv_pairs(params);
	return 0;
}

int common_dspaces_rank(void)
{
	if (dcg)
		return dcg_get_rank(dcg);
	else return -1;
}

int common_dspaces_peers(void)
{
	if (dcg)
		return dcg_get_num_peers(dcg);
	else return -1;
}

int common_dspaces_servers(void)
{
        if (dcg)
                return dcg_get_num_servers(dcg);
        else return -1;
}


int common_dspaces_get_num_space_peers(void)
{
	if (dcg)
		return dcg_get_num_space_peers(dcg);
	else return -1;
}

void common_dspaces_barrier(void)
{
    if (!is_dspaces_lib_init()) return;

	int err = dcg_barrier(dcg);
	if (err < 0) 
		ERROR_TRACE_AND_EXIT();
}

void common_dspaces_lock_on_read(const char *lock_name, void *comm)
{
    if (!is_dspaces_lib_init()) return;

    //printf("Requesting read lock on %s\n", lock_name);

	int err = dcg_lock_on_read(lock_name, comm);
	if (err < 0) 
		ERROR_TRACE_AND_EXIT();

	//printf("Got read lock on %s\n", lock_name);
}

void common_dspaces_unlock_on_read(const char *lock_name, void *comm)
{
    if (!is_dspaces_lib_init()) return;

    //printf("Releasing read lock on %s\n", lock_name);

	int err = dcg_unlock_on_read(lock_name, comm);
	if (err < 0) 
		ERROR_TRACE_AND_EXIT();

	//printf("Released read lock on %s\n", lock_name);
}

void common_dspaces_lock_on_write(const char *lock_name, void *comm)
{
    if (!is_dspaces_lib_init()) return;

    //printf("Requesting write lock on %s\n", lock_name);

	int err = dcg_lock_on_write(lock_name, comm);
	if (err < 0)
		ERROR_TRACE_AND_EXIT();

	//printf("Got write lock on %s\n", lock_name);
}

void common_dspaces_unlock_on_write(const char *lock_name, void *comm)
{
    if (!is_dspaces_lib_init()) return;

    //printf("Releasing write lock on %s\n", lock_name);

	int err = dcg_unlock_on_write(lock_name, comm);
	if (err < 0)
		ERROR_TRACE_AND_EXIT();

	//printf("Released write lock on %s\n", lock_name);

}

void common_dspaces_define_gdim(const char *var_name, int ndim, uint64_t *gdim)
{
    if (!is_dspaces_lib_init() || !is_ndim_within_bound(ndim)) return;
    update_gdim_list(&dcg->gdim_list, var_name, ndim, gdim);
}

int common_dspaces_get(const char *var_name,
	unsigned int ver, int size,
	int ndim,
	uint64_t *lb,
	uint64_t *ub,
	void *data)
{
    if (!is_dspaces_lib_init() || !is_ndim_within_bound(ndim)) {
        return -EINVAL;
    }

    struct obj_descriptor odsc = {
            .version = ver, .owner = -1, 
            .st = st,
            .size = size,
            .bb = {.num_dims = ndim,}
    };
    memset(odsc.bb.lb.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);
    memset(odsc.bb.ub.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);

    memcpy(odsc.bb.lb.c, lb, sizeof(uint64_t)*ndim);
    memcpy(odsc.bb.ub.c, ub, sizeof(uint64_t)*ndim);

    struct obj_data *od;
    int err = -ENOMEM;

    strncpy(odsc.name, var_name, sizeof(odsc.name)-1);
    odsc.name[sizeof(odsc.name)-1] = '\0';

    od = obj_data_alloc_no_data(&odsc, data);
    if (!od) {
        uloga("'%s()': failed, can not allocate data object.\n", 
            __func__);
        return -ENOMEM;
    }

    // set global dimension
    set_global_dimension(&dcg->gdim_list, var_name, &dcg->default_gdim,
                         &od->gdim);
    err = dcg_obj_get(od);
    obj_data_free(od);
    if (err < 0 && err != -EAGAIN) 
        uloga("'%s()': failed with %d, can not get data object.\n",
            __func__, err);

    return err;
}

char* common_dspaces_get_latest_meta(unsigned int ver, char *name, int *nVars, int *version)
{
    int err = -ENOMEM;
   char *data;
    data = dcg_obj_get_meta(1,ver, name, nVars, version);
    if(!data){
        uloga("'%s()': failed with %d, can not get data object.\n",
            __func__, err);
	 return NULL;
	}
	return data;
}

char* common_dspaces_get_next_meta(unsigned int ver, char *name, int *nVars, int *version)
{
    int err = -ENOMEM;
    char *data;
    data = dcg_obj_get_meta(0, ver, name, nVars, version);
    if(!data){
       // uloga("'%s()': failed with %d, can not get data object.\n",
       //     __func__, err);
    	return NULL;
	}
	return data;
}

#ifdef SHMEM_OBJECTS
int common_dspaces_put(const char *var_name, 
        unsigned int ver, int size,
        int ndim,
        uint64_t *lb,
        uint64_t *ub,
        const void *data)
{
        if (!is_dspaces_lib_init() || !is_ndim_within_bound(ndim)) {
            return -EINVAL;
        }
        struct obj_descriptor odsc_big= {
                    .version = ver, .owner = -1, 
                    .st = st,
                    .size = size,
                    .bb = {.num_dims = ndim,}
                };
        memset(odsc_big.bb.lb.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);
        memset(odsc_big.bb.ub.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);

        memcpy(odsc_big.bb.lb.c, lb, sizeof(uint64_t)*ndim);
        memcpy(odsc_big.bb.ub.c, ub, sizeof(uint64_t)*ndim);
        int shmem_max_size = 268435456;
        //int shmem_max_size = 4096;
        if (obj_data_size(&odsc_big) <= shmem_max_size){
            struct obj_data *od;
            int err = -ENOMEM;
            strncpy(odsc_big.name, var_name, sizeof(odsc_big.name)-1);
            odsc_big.name[sizeof(odsc_big.name)-1] = '\0';
            od = obj_data_alloc_with_data(&odsc_big, data);
            if (!od) {
                uloga("'%s()': failed, can not allocate data object.\n", 
                    __func__);
                    return -ENOMEM;
            }
            set_global_dimension(&dcg->gdim_list, var_name, &dcg->default_gdim,
                         &od->gdim);
            err = dcg_obj_put(od);
            if (err < 0) {
                obj_data_free(od);
                uloga("'%s()': failed with %d, can not put data object.\n", 
                    __func__, err);
                return err;
            }
            sync_op_id = err;
        }else{

            int total_blocks = ceil(obj_data_size(&odsc_big)/shmem_max_size);
            int split_dim = -1;
            int i;
            int block_size;
            for (i = ndim-1; i >=0 ; i--)
            {
                block_size = ceil((double)(ub[i]-lb[i]+1)/total_blocks);
                if(block_size >= 1){
                    split_dim = i;
                    uloga("Block Size is %d", block_size);
                    break;
                }
            }
            if(split_dim == -1){
                uloga("This should not happen \n");
                return -1;
            }
            for (i = 0; i < total_blocks; ++i)
                {
                    struct obj_descriptor odsc = {
                        .version = ver, .owner = -1, 
                        .st = st,
                        .size = size,
                        .bb = {.num_dims = ndim,}
                    };
                    memset(odsc.bb.lb.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);
                    memset(odsc.bb.ub.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);
                    int j;
                    for (j = ndim-1; j >= 0; j--)
                    {  
                        if(j == split_dim){
                            odsc.bb.lb.c[j] = lb[j]+i*block_size;
                            odsc.bb.ub.c[j] = odsc.bb.lb.c[j] + block_size -1;
                            if(odsc.bb.ub.c[j]>ub[j])
                                odsc.bb.ub.c[j] = ub[j];
                        }else{
                            odsc.bb.lb.c[j] = odsc_big.bb.lb.c[j];
                            odsc.bb.ub.c[j] = odsc_big.bb.ub.c[j];
                        }
                    }  
                    struct obj_data *od;
                    int err = -ENOMEM;
                    strncpy(odsc.name, var_name, sizeof(odsc.name)-1);
                    odsc.name[sizeof(odsc.name)-1] = '\0';
                    od = obj_data_alloc_with_data_split(&odsc, data, &odsc_big);
                    if (!od) {
                        uloga("'%s()': failed, can not allocate data object.\n", 
                            __func__);
                            return -ENOMEM;
                    }
                    set_global_dimension(&dcg->gdim_list, var_name, &dcg->default_gdim,
                                 &od->gdim);
                    err = dcg_obj_put(od);
                    if (err < 0) {
                        obj_data_free(od);
                        uloga("'%s()': failed with %d, can not put data object.\n", 
                            __func__, err);
                        return err;
                    }
                    sync_op_id = err;
                }


        }
        return 0;
}
#else
int common_dspaces_put(const char *var_name, 
        unsigned int ver, int size,
        int ndim,
        uint64_t *lb,
        uint64_t *ub,
        const void *data)
{
#if defined(DS_HAVE_DSPACES_LOCATION_AWARE_WRITE)
        return common_dspaces_put_location_aware(var_name, ver, size, ndim,
                    lb, ub, data);
#else
        if (!is_dspaces_lib_init() || !is_ndim_within_bound(ndim)) {
            return -EINVAL;
        }

        struct obj_descriptor odsc = {
                .version = ver, .owner = -1, 
                .st = st,
                .size = size,
                .bb = {.num_dims = ndim,}
        };

        memset(odsc.bb.lb.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);
        memset(odsc.bb.ub.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);

        memcpy(odsc.bb.lb.c, lb, sizeof(uint64_t)*ndim);
        memcpy(odsc.bb.ub.c, ub, sizeof(uint64_t)*ndim);

        struct obj_data *od;
        int err = -ENOMEM;

        strncpy(odsc.name, var_name, sizeof(odsc.name)-1);
        odsc.name[sizeof(odsc.name)-1] = '\0';

        od = obj_data_alloc_with_data(&odsc, data);
        if (!od) {
            uloga("'%s()': failed, can not allocate data object.\n", 
                __func__);
                return -ENOMEM;
        }

        // set global dimension
        set_global_dimension(&dcg->gdim_list, var_name, &dcg->default_gdim,
                             &od->gdim); 
        err = dcg_obj_put(od);
        if (err < 0) {
            obj_data_free(od);
            uloga("'%s()': failed with %d, can not put data object.\n", 
                __func__, err);
            return err;
        }
        sync_op_id = err;

        return 0;
#endif
}

#define MAX_NUM_PEER_PER_NODE 64
#if defined(DS_HAVE_DSPACES_LOCATION_AWARE_WRITE)
int common_dspaces_put_location_aware(const char *var_name, 
        unsigned int ver, int size,
        int ndim,
        uint64_t *lb,
        uint64_t *ub,
        void *data)
{
        if (!is_dspaces_lib_init() || !is_ndim_within_bound(ndim)) {
            return -EINVAL;
        }

        struct obj_descriptor odsc = {
                .version = ver, .owner = -1, 
                .st = st,
                .size = size,
                .bb = {.num_dims = ndim,}
        };

        memset(odsc.bb.lb.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);
        memset(odsc.bb.ub.c, 0, sizeof(uint64_t)*BBOX_MAX_NDIM);

        memcpy(odsc.bb.lb.c, lb, sizeof(uint64_t)*ndim);
        memcpy(odsc.bb.ub.c, ub, sizeof(uint64_t)*ndim);

        struct obj_data *od;
        int err = -ENOMEM;

        strncpy(odsc.name, var_name, sizeof(odsc.name)-1);
        odsc.name[sizeof(odsc.name)-1] = '\0';

        od = obj_data_alloc_with_data(&odsc, data);
        if (!od) {
            uloga("'%s()': failed, can not allocate data object.\n", 
                __func__);
                return -ENOMEM;
        }

        // set global dimension
        set_global_dimension(&dcg->gdim_list, var_name, &dcg->default_gdim,
                             &od->gdim); 

        // find dataspaces servers running on local compute node
        struct node_id* peer_tab[MAX_NUM_PEER_PER_NODE];
        int local_server_ids[MAX_NUM_PEER_PER_NODE];
        int num_space_srv = 0, num_local_peer = 0, num_local_server = 0;
        num_space_srv = dcg_get_num_space_peers(dcg);
        rpc_server_find_local_peers(dcg->dc->rpc_s, peer_tab,
            &num_local_peer, MAX_NUM_PEER_PER_NODE);
        int i, j;
        for (i = j = 0; i < num_local_peer; i++) {
            if (peer_tab[i]->ptlmap.id < num_space_srv) {
                local_server_ids[j++] = peer_tab[i]->ptlmap.id;
                num_local_server++;        
            }
        }

        if (num_local_server == 0) {
            err = dcg_obj_put(od);
        } else {
            // select on local server to put the data
            int server_id = local_server_ids[dcg->dc->rpc_s->ptlmap.id % num_local_server];
            // Application can use dspaces_get() to fetch the data.
            err = dcg_obj_put_to_server(od, server_id);
        }
        if (err < 0) {
            obj_data_free(od);
            uloga("'%s()': failed with %d, can not put data object.\n", 
                __func__, err);
            return err;
        }
        sync_op_id = err;

        return 0;
}
#endif /* DS_HAVE_DSPACES_LOCATION_AWARE_WRITE */
#endif /* SHM_OBJECTS */

int common_dspaces_remove(const char *var_name, unsigned int ver)
{
    if (!is_dspaces_lib_init()) {
        return 0;
    }

    int err = dcg_remove(var_name, ver);
    if (err < 0)
         ERROR_TRACE_AND_EXIT();
    return err;
}

int common_dspaces_put_sync(void)
{
	if (!is_dspaces_lib_init()) {
		return -EINVAL;
	}

    int err = dcg_obj_sync(sync_op_id);
    if (err < 0)
        uloga("'%s()': failed with %d, can not complete put_sync.\n", 
			__func__, err);

    return err;
}

#ifdef DS_HAVE_ACTIVESPACE
int common_dspaces_code_load(void *fnaddr, // int off, int size_code, 
	const char *var_name, unsigned int version, int size_elem,
	int xl, int yl, int zl,
	int xu, int yu, int zu,
	void *data)
{
        struct obj_descriptor odsc = {
                .version = version, .owner = -1, 
                .st = st,
                .size = size_elem,
                .bb = {.num_dims = num_dims, 
                       .lb.c = {xl, yl, zl}, 
                       .ub.c = {xu, yu, zu}}};
        struct obj_data *od;
        int err = -ENOMEM;

	if (!dcg) {
		uloga("'%s()': library was not properly initialized!\n",
			 __func__);
		return -EINVAL;
	}

	strncpy(odsc.name, var_name, sizeof(odsc.name)-1);
	odsc.name[sizeof(odsc.name)-1] = '\0';

	od = obj_data_alloc_no_data(&odsc, data);
	if (!od)
		goto err_out;

	err = dcg_code_send(fnaddr, /*off, size_code,*/ od);
	obj_data_free(od);
	if (err >= 0)
		return err;

 err_out:
	ERROR_TRACE();
}
#endif // end of #ifdef DS_HAVE_ACTIVESPACE

void common_dspaces_finalize(void)
{
	if (!is_dspaces_lib_init()) {
		return;
	}

#ifdef DS_HAVE_DIMES
    dimes_client_free();
#endif
    dcg_free(dcg);
    dcg = 0;
}

void common_dspaces_kill(void)
{
	dcgrpc_kill(dcg);
}

#ifdef DS_HAVE_DIMES
void common_dimes_define_gdim(const char *var_name, int ndim, uint64_t *gdim)
{
    if (!is_dimes_lib_init() || !is_ndim_within_bound(ndim)) return;
    update_gdim_list(&dimes_c->gdim_list, var_name, ndim, gdim);
}

int common_dimes_get(const char *var_name,
        unsigned int ver, int size,
        int ndim,
        uint64_t *lb,
        uint64_t *ub,
        void *data)
{
    if (!is_dimes_lib_init() || !is_ndim_within_bound(ndim)) {
        return -EINVAL;
    }

    return dimes_client_get(var_name, ver, size,
                ndim, lb, ub, data);
}

int common_dimes_put(const char *var_name,
        unsigned int ver, int size,
        int ndim,
        uint64_t *lb,
        uint64_t *ub,
        void *data)
{
    if (!is_dimes_lib_init() || !is_ndim_within_bound(ndim)) {
        return -EINVAL;
    }

    return dimes_client_put(var_name, ver, size,
                ndim, lb, ub, data);
}

int common_dimes_put_sync_all(void)
{
    if (!is_dimes_lib_init()) return -EINVAL;
    return dimes_client_put_sync_all();
}

int common_dimes_put_set_group(const char *group_name, int step)
{
    if (!is_dimes_lib_init()) return -EINVAL;
    return dimes_client_put_set_group(group_name, step);
}

int common_dimes_put_unset_group()
{
    if (!is_dimes_lib_init()) return -EINVAL;
    return dimes_client_put_unset_group();
}

int common_dimes_put_sync_group(const char *group_name, int step)
{
    if (!is_dimes_lib_init()) return -EINVAL;
    return dimes_client_put_sync_group(group_name, step);
}

#ifdef DS_HAVE_DIMES_SHMEM
int common_dimes_shmem_init(void *comm, size_t shmem_obj_size) {
    return dimes_client_shmem_init(comm, shmem_obj_size);
}

int common_dimes_shmem_finalize(unsigned int unlink) {
    return dimes_client_shmem_finalize(unlink);
}

int common_dimes_shmem_checkpoint() {
    return dimes_client_shmem_checkpoint();
}

int common_dimes_shmem_restart(void *comm) {
    return dimes_client_shmem_restart(comm);
}

int common_dimes_shmem_clear() {
    return dimes_client_shmem_clear_testing();
}

int common_dimes_shmem_reset_server_state(int server_id)
{
    return dimes_client_shmem_reset_server_state(server_id);
}

int common_dimes_shmem_update_server_state()
{
    return dimes_client_shmem_update_server_state();
}

uint32_t common_dimes_shmem_get_nid()
{
    return dimes_client_shmem_get_nid();
}

int common_dimes_shmem_get_node_rank()
{
    return dimes_client_shmem_get_node_rank();
}

MPI_Comm common_dimes_shmem_get_node_mpi_comm()
{
    return dimes_client_shmem_get_node_mpi_comm();
}

int common_dimes_shmem_put_local(const char *var_name,
        unsigned int ver, int size,
        int ndim,
        uint64_t *lb,
        uint64_t *ub,
        void *data)
{
    if (!is_dimes_lib_init() || !is_ndim_within_bound(ndim)) {
        return -EINVAL;
    }

    return dimes_client_shmem_put_local(var_name, ver, size,
                ndim, lb, ub, data);
}

int common_dimes_shmem_get_local(const char *var_name,
        unsigned int ver, int size,
        int ndim,
        uint64_t *lb,
        uint64_t *ub,
        void *data)
{
    if (!is_dimes_lib_init() || !is_ndim_within_bound(ndim)) {
        return -EINVAL;
    }

    return dimes_client_shmem_get_local(var_name, ver, size,
                ndim, lb, ub, data);
}
#endif

#endif

void common_dspaces_set_mpi_rank_hint(int rank)
{
    dcg_set_mpi_rank_hint(rank);
}

void common_dspaces_unset_mpi_rank_hint()
{
    dcg_unset_mpi_rank_hint();
}
