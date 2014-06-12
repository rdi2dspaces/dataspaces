#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"
#include "hstaging_api.h"
#include "mpi.h"

#include "hpgv.h"
#include "carraysearch.h"

struct viz_task_info {
    int is_viz_init;
    double *viz_data;
    // int num_viz_step;
};
// Information kept for rendering task
struct viz_task_info viz_info;

struct parallel_communicator {
    MPI_Comm comm;
};

typedef int (*task_function)(struct task_descriptor *t, struct parallel_communicator *comm);
struct parallel_task {
	int appid;
	task_function func_ptr;
};
static struct parallel_task ptasks[MAX_NUM_TASKS+1];
static int num_tasks = 0;
static struct g_info g;
static MPI_Comm origin_mpi_comm; 

static void viz_task_info_init(struct viz_task_info *p)
{
    p->is_viz_init = 0;
    p->viz_data = NULL;
}

static void viz_task_info_free(struct viz_task_info *p)
{
    if (p->is_viz_init && p->viz_data) {
        free(p->viz_data);
    }
}

static int communicator_init(MPI_Comm comm)
{
    viz_task_info_init(&viz_info);
    origin_mpi_comm = comm;

    return 0;    
}

static int communicator_free()
{
    viz_task_info_free(&viz_info);
    return 0;    
}

static struct parallel_communicator* create_parallel_comm(struct task_descriptor *t)
{
    struct parallel_communicator *comm = malloc(sizeof(*comm));
    MPI_Group origin_mpi_comm_group;
    MPI_Group new_group;

    MPI_Comm_group(origin_mpi_comm, &origin_mpi_comm_group);
    MPI_Group_incl(origin_mpi_comm_group, t->nproc, t->bk_mpi_rank_tab, &new_group);
    MPI_Comm_create_group(origin_mpi_comm, new_group, 0, &comm->comm); 

    return comm;
}

static void free_parallel_comm(struct parallel_communicator *comm) {
    if (comm) {
        free(comm);
    }
}

static void task_done(struct task_descriptor *t, struct parallel_communicator *comm)
{
    if (comm) {
        MPI_Barrier(comm->comm);
        if (t->rank == 0) { 
            hstaging_set_task_finished(t);
        }
    } else {
        uloga("%s(): error comm == NULL\n", __func__);
    }
}

static int register_task_function(int appid, task_function func_ptr)
{
	ptasks[num_tasks].appid = appid;
	ptasks[num_tasks].func_ptr = func_ptr;
	num_tasks++;
}

static int exec_task_function(struct task_descriptor *t)
{
	int i, err;
	for (i = 0; i < num_tasks; i++) {
		if (t->appid == ptasks[i].appid) {
            //double t1, t2;
            //t1 = MPI_Wtime();
            struct parallel_communicator *comm = create_parallel_comm(t);	
            err = (ptasks[i].func_ptr)(t, comm);
            task_done(t, comm); 
            free_parallel_comm(comm); 
            //t2 = MPI_Wtime();
            //uloga("%s(): task appid= %d execution time %lf\n", __func__, t->appid, t2-t1);

            if (t->bk_mpi_rank_tab) {
                free(t->bk_mpi_rank_tab);
            }
            if (t->vars) {
                free(t->vars);
            }
            return err;
		}
	}

	uloga("%s(): unknown task appid= %d\n", __func__, t->appid);
}

static int update_var(const char *var_name, int version)
{
    int err;
    struct hstaging_var var_desc;
    int *c = NULL;
    size_t elem_size = sizeof(double);

    strcpy(var_desc.name, var_name);
    var_desc.version = version;
    var_desc.bb.num_dims = g.dims;
    var_desc.elem_size = elem_size;
    c = var_desc.bb.lb.c;
    c[0] = 0;
    c[1] = 0;
    c[2] = 0;
    c = var_desc.bb.ub.c;
    c[0] = (g.npx * g.spx) - 1;
    c[1] = (g.npy * g.spy) - 1;
    c[2] = (g.npz * g.spz) - 1;
    err = hstaging_update_var(&var_desc, OP_PUT);
    return err;
}

static int update_var_nodata(const char *var_name, int version)
{
    int err;
    struct hstaging_var var_desc;
    int *c = NULL;
    size_t elem_size = sizeof(double);

    strcpy(var_desc.name, var_name);
    var_desc.version = version;
    var_desc.bb.num_dims = 3;
    var_desc.elem_size = elem_size;
    c = var_desc.bb.lb.c;
    c[0] = 0;
    c[1] = 0;
    c[2] = 0;
    c = var_desc.bb.ub.c;
    c[0] = 0;
    c[1] = 0;
    c[2] = 0;
    err = hstaging_update_var(&var_desc, OP_PUT);
    return err;
}

static void set_data_decomposition(struct task_descriptor *t, struct hstaging_var *var_desc)
{
	g.rank = t->rank;
	g.dims = var_desc->bb.num_dims;

    int num_peer, dims, dimx, dimy, dimz;
    char fname[128];
    sprintf(fname, "task%d.conf", t->appid);
    int err = read_task_info(fname, &num_peer, &g.npx, &g.npy, &g.npz,
                        &dims, &dimx, &dimy, &dimz);
    if (err < 0 ) {
        uloga("Failed to read %s\n", fname);
    }

	if (g.dims == 2) {
		g.spx = (var_desc->bb.ub.c[0]+1)/g.npx;
		g.spy = (var_desc->bb.ub.c[1]+1)/g.npy;
		g.spz = 0;
	} else if (g.dims == 3) {
		g.spx = (var_desc->bb.ub.c[0]+1)/g.npx;
		g.spy = (var_desc->bb.ub.c[1]+1)/g.npy;
		g.spz = (var_desc->bb.ub.c[2]+1)/g.npz;
	}

    /*
	uloga("%s(): g.npx= %d g.npy= %d g.npz= %d g.spx= %d g.spy= %d g.spz= %d\n",
		__func__,g.npx,g.npy,g.npz,g.spx,g.spy,g.spz);
    */
}

static int read_input_data(struct task_descriptor *t)
{
	int i;
	int err;

	for (i = 0; i < t->num_vars; i++) {
		double *databuf = NULL;
		int xl, yl, zl, xu, yu, zu;
		size_t elem_size = sizeof(double);
		struct hstaging_var *var_desc = &(t->vars[i]);
        if (var_desc->type != var_type_depend) continue;

		set_data_decomposition(t, var_desc);
		if (var_desc->bb.num_dims == 2) {
			databuf = allocate_data(g.spx*g.spy);					
		} else if (var_desc->bb.num_dims == 3) {
			databuf = allocate_data(g.spx*g.spy*g.spz);
		}		
		generate_bbox(&g, &xl, &yl, &zl, &xu, &yu, &zu);
        err = hstaging_get_var(var_desc->name, var_desc->version, elem_size,
                xl, yl, zl, xu, yu, zu, databuf, NULL);	
		if (err < 0) {
			uloga("%s(): failed to get var\n", __func__);
			return -1;
		}

		if (var_desc->bb.num_dims == 2) {
			compute_stats(var_desc->name, databuf, g.spx*g.spy, t->rank);
		} else if (var_desc->bb.num_dims == 3) {
			compute_stats(var_desc->name, databuf, g.spx*g.spy*g.spz,t->rank);
		}

		free(databuf);
	}

	uloga("%s(): task wid= %u tid= %u appid= %d rank= %d nproc= %d "
		"num_vars= %d\n", __func__, t->wid, t->tid, t->appid, 
        t->rank, t->nproc, t->num_vars);
	
	return 0;
}

static int write_output_data(struct task_descriptor *t, const char *var_name, struct parallel_communicator *comm, int version)
{
	double *databuf = NULL;
	int elem_size, num_elem, xl, yl, zl, xu, yu, zu;
	int err;

	int num_peer, dimx, dimy, dimz;
	char fname[128];
	sprintf(fname, "task%d.conf", t->appid);
	err = read_task_info(fname, &num_peer, &g.npx, &g.npy, &g.npz,
						&g.dims, &dimx, &dimy, &dimz);
	if (err < 0 ) {
		uloga("Failed to read %s\n", fname);
		return err;
	}

	if (t->nproc != num_peer) {
		uloga("Warning: t->nproc= %d but num_peer= %d\n", t->nproc, num_peer);
	}

	if (g.dims == 2) {
		g.spx = dimx / g.npx;
		g.spy = dimy / g.npy;
		num_elem = g.spx * g.spy;
	} else if (g.dims == 3) {
		g.spx = dimx / g.npx;
		g.spy = dimy / g.npy;
		g.spz = dimz / g.npz;
		num_elem = g.spx * g.spy * g.spz;
	}
	databuf = allocate_data(num_elem);
	generate_data(databuf, version, num_elem);
	elem_size = sizeof(double);
	generate_bbox(&g, &xl, &yl, &zl, &xu, &yu, &zu);

	err = hstaging_put_var(var_name, version, elem_size,
			xl, yl, zl, xu, yu, zu, databuf, &comm->comm);
	if (err < 0) {
		free(databuf);
		return err;
	}
	
	if (t->rank == 0) {
		update_var(var_name, version);
	}

	free(databuf);
	return err;
}

void print_task_info(const struct task_descriptor *t)
{
    uloga("%s(): task wid= %u tid= %u appid= %d rank= %d nproc= %d num_vars= %d\n", __func__,
        t->wid, t->tid, t->appid, t->rank, t->nproc, t->num_vars);

    int i;
    for (i = 0; i < t->num_vars; i++) {
        uloga("%s(): task wid= %u tid= %u appid= %d var '%s' version %d elem_size %d\n",
            __func__, t->wid, t->tid, t->appid, t->vars[i].name,
            t->vars[i].version, t->vars[i].elem_size);
    } 
}

int dag_task(struct task_descriptor *t, struct parallel_communicator *comm)
{
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);

    // Print input variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    unsigned int seconds = t->appid; // TODO: fixme
    sleep(seconds);

    MPI_Barrier(comm->comm);

    if (t->rank == 0) {
        char var_name[MAX_VAR_NAME_LEN];
        sprintf(var_name, "task%d_output_var", t->appid);
        update_var_nodata(var_name, 0);
    }
        
    return 0;
}

static int sml_mstep = 10;
int xgc1_task(struct task_descriptor *t, struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print variable information
    if (t->rank == 0) {
        print_task_info(t);
    }
    
    // read particle data
    if (comm_rank == 0) {
        uloga("%s(): read particle data\n", __func__);
    }

    // sleep for 1 second
    unsigned int seconds = 1;
    int ts;
    for (ts = 1; ts <= sml_mstep; ts++) {
        sleep(seconds);
        // write turbulence data
        if (comm_rank == 0) {
            uloga("%s(): ts %d write turbulence data\n", __func__, ts);
        }
    }

    // write particle data
    if (comm_rank == 0) {
        uloga("%s(): write particle data\n", __func__);
    }

    MPI_Barrier(comm->comm);
    t2 = MPI_Wtime();

    if (t->rank == 0) {
        update_var_nodata("xgc1_output", 0);
    }

    if (t->rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

int xgca_task(struct task_descriptor *t, struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    // read particle data
    if (comm_rank == 0) {
        uloga("%s(): read particle data\n", __func__);
    }

    // sleep for 1 second
    unsigned int seconds = 1;
    int ts;
    for (ts = 1; ts <= sml_mstep; ts++) {
        // read turbulence data
        if (comm_rank == 0) {
            uloga("%s(): ts %d read turbulence data\n", __func__, ts);
        }
        sleep(seconds);
    }

    // write particle data
    if (comm_rank == 0) {
        uloga("%s(): write particle data\n", __func__);
    }

    MPI_Barrier(comm->comm);
    t2 = MPI_Wtime();

    if (t->rank == 0) {
        update_var_nodata("xgca_output", 0);
    }
    if (t->rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

static int s3d_num_ts = 10;
int s3d_task(struct task_descriptor *t,
    struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    // sleep for 1 second
    unsigned int seconds = 3;
    int ts;
    uint32_t wid = S3D_WORKFLOW_ID;
    uint32_t tid = 2;
    for (ts = 1; ts <= s3d_num_ts; ts++) {
        if (comm_rank == 0) {
            uloga("%s(): ts %d\n", __func__, ts);
        }
        sleep(seconds);

        if (comm_rank == 0) {
            // submit analyis operation
            // TODO: make is non-blocking
            hstaging_submit_task(wid, tid++, "s3d_viz.conf");
            hstaging_submit_task(wid, tid++, "s3d_stat.conf");
            hstaging_submit_task(wid, tid++, "s3d_topo.conf");
            hstaging_submit_task(wid, tid++, "s3d_indexing.conf");
        }
        MPI_Barrier(comm->comm);
    }

    MPI_Barrier(comm->comm);
    t2 = MPI_Wtime();

    if (t->rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

int s3d_viz_task(struct task_descriptor *t,
    struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print input variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    // sleep for 1 second
    unsigned int seconds = 1;
    sleep(seconds);

    MPI_Barrier(comm->comm);
    t2 = MPI_Wtime();
    if (comm_rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

int s3d_stat_task(struct task_descriptor *t,
    struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print input variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    // sleep for 1 second
    unsigned int seconds = 1;
    sleep(seconds);

    MPI_Barrier(comm->comm);
    t2 = MPI_Wtime();
    if (comm_rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

int s3d_topo_task(struct task_descriptor *t,
    struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print input variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    // sleep for 2 second
    unsigned int seconds = 2;
    sleep(seconds);

    MPI_Barrier(comm->comm);
    t2 = MPI_Wtime();
    if (comm_rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

int s3d_indexing_task(struct task_descriptor *t,
    struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print input variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    // sleep for 1 second
    unsigned int seconds = 1;
    sleep(seconds);

    MPI_Barrier(comm->comm);
    t2 = MPI_Wtime();
    if (comm_rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

static int dns_les_num_ts = 10;
static int dns_les_num_sub_step = 6;
int dns_task(struct task_descriptor *t,
    struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print input variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    // sleep for 1 second
    unsigned int seconds = 1;
    int ts;
    for (ts = 1; ts <= dns_les_num_ts; ts++) {
        int i;
        for (i = 1; i <= dns_les_num_sub_step; i++) {
            if (comm_rank == 0) {
                uloga("%s(): step %d substep %d\n", __func__, ts, i);
            }
            sleep(seconds);
        }
        MPI_Barrier(comm->comm);
    }

    t2 = MPI_Wtime();
    
    if (t->rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

int les_task(struct task_descriptor *t,
    struct parallel_communicator *comm)
{
    double t1, t2;
    int comm_size, comm_rank;
    MPI_Barrier(comm->comm);
    MPI_Comm_size(comm->comm, &comm_size);
    MPI_Comm_rank(comm->comm, &comm_rank);
    t1 = MPI_Wtime();

    // print input variable information
    if (t->rank == 0) {
        print_task_info(t);
    }

    // sleep for 1 second
    unsigned int seconds = 1;
    int ts;
    for (ts = 1; ts <= dns_les_num_ts; ts++) {
        int i;
        for (i = 1; i <= dns_les_num_sub_step; i++) {
            if (comm_rank == 0) {
                uloga("%s(): step %d substep %d\n", __func__, ts, i);
            }
            sleep(seconds);
        }
        MPI_Barrier(comm->comm);
    }

    t2 = MPI_Wtime();

    if (t->rank == 0) {
        uloga("%s(): execution time %lf\n", __func__, t2-t1);
    }
    return 0;
}

int task1(struct task_descriptor *t, struct parallel_communicator *comm)
{
	read_input_data(t);
	// Do some computation
	write_output_data(t, "topology_var2", comm, 0);
	return 0;
}

int task2(struct task_descriptor *t, struct parallel_communicator *comm)
{
	read_input_data(t);
	// Do some computation
	write_output_data(t, "stat_var2", comm, 0);
	return 0;
} 

int task3(struct task_descriptor *t, struct parallel_communicator *comm)
{
	read_input_data(t);
	// Do some computation
	write_output_data(t, "viz_var2", comm, 0);
	return 0;
} 

int task4(struct task_descriptor *t, struct parallel_communicator *comm)
{
	read_input_data(t);
	// Do some computation
	return 0;
} 

int task5(struct task_descriptor *t, struct parallel_communicator *comm)
{
	read_input_data(t);
	// Do some computation
	return 0;
} 

int task6(struct task_descriptor *t, struct parallel_communicator *comm)
{
	read_input_data(t);
	// Do some computation
	return 0;
}

#define RENDER_VAR 3
float my_data_quantize(float value, int varname)
{
    float min, max;

    if (3 == RENDER_VAR) {
        //min = 2.5;
        //max = 15;
        min = 1.22;
        max = 7.2;
    }

    if (4 == RENDER_VAR) {
        min = 1.22;
        max = 7.2;
    }

    //min = -5.5e-04;
    //max = 5.5e-04;

    if( max == min) {
        return value;
    }

    float v = (value - min) / (max - min);

    if (v < 0) {
        v = 0;
    }
    if (v > 1) {
        v = 1;
    }
    return v;
}

// TODO: this task routine is highly hard-coded...
int task_viz_render(struct task_descriptor *t, struct parallel_communicator *comm)
{
    int err;
    int npx, npy, npz, mypx, mypy, mypz;
    int domain_grid_size[3];
    int render_root = 0;
    double tm_st, tm_end;

    MPI_Comm_rank(comm->comm, &t->rank);
    MPI_Barrier(comm->comm);
    tm_st = MPI_Wtime();

    // read input data
    double *data = NULL;
    int xl, yl, zl, xu, yu, zu;
    size_t elem_size = sizeof(double);
    struct hstaging_var *var_desc;

    var_desc = &(t->vars[0]);
    set_data_decomposition(t, var_desc);
    if (!viz_info.is_viz_init) {
        data = allocate_data(g.spx*g.spy*g.spz);
        viz_info.viz_data = data;
    } else {
        data = viz_info.viz_data;
    }
    generate_bbox(&g, &xl, &yl, &zl, &xu, &yu, &zu);
    err = hstaging_get_var(var_desc->name, var_desc->version, elem_size,
            xl, yl, zl, xu, yu, zu, data, NULL);
    if (err < 0) {
        uloga("%s(): failed to get var\n", __func__);
        return -1;
    }

    MPI_Barrier(comm->comm);
    tm_end = MPI_Wtime();
    if (t->rank == 0) {
        uloga("%s(): fetch data time %lf\n", __func__, tm_end-tm_st);
    }

    tm_st = tm_end;
    // perform viz. render
    npx = g.npx;
    npy = g.npy;
    npz = g.npz;
    domain_grid_size[0] = g.npx * g.spx;
    domain_grid_size[1] = g.npy * g.spy;
    domain_grid_size[2] = g.npz * g.spz;
    mypx = g.rank % g.npx;
    mypy = g.rank / g.npx % g.npy;
    mypz = g.rank / g.npx / g.npy;

    //uloga("%s(): step %d rank %d npx %d npy %d npz %d "
    //    "domain_grid_size[] = {%d, %d, %d} "
    //    "mypx %d mypy %d mypz %d\n", __func__,
    //    t->step, g.rank, npx, npy, npz, 
    //    domain_grid_size[0], domain_grid_size[1], domain_grid_size[2],
    //    mypx, mypy, mypz);

    // compute_stats(var_desc->var_name, data, g.spx*g.spy*g.spz, t->rank);
    if (!viz_info.is_viz_init) {
        hpgv_insituvis_init_(g.rank, comm->comm, render_root,
                    npx, npy, npz, mypx, mypy, mypz,
                    domain_grid_size, data);
        viz_info.is_viz_init = 1;
    }

    hpgv_insitu_render_tstep_(g.rank, comm->comm, var_desc->version, my_data_quantize);

    MPI_Barrier(comm->comm);
    tm_end = MPI_Wtime();
    if (t->rank == 0) {
        uloga("%s(): render data time %lf\n", __func__, tm_end-tm_st);
    }

    return 0; 
}

// TODO: use read_input_data() to fetch input data buffers
int task_fb_indexing(struct task_descriptor *t, struct parallel_communicator *comm)
{
    int err;
    double tm_st, tm_end, tm_end1;

    MPI_Comm_rank(comm->comm, &t->rank);

    // read input data
    MPI_Barrier(comm->comm);
    tm_st = MPI_Wtime();

    double *data = NULL;
    int xl, yl, zl, xu, yu, zu;
    struct hstaging_var *var_desc = &(t->vars[0]);
    size_t elem_size = var_desc->elem_size;
    int num_double_elem = 0;

    set_data_decomposition(t, var_desc);
    num_double_elem = (g.spx*g.spy*g.spz)*elem_size/sizeof(double);
    data = allocate_data(num_double_elem);
    generate_bbox(&g, &xl, &yl, &zl, &xu, &yu, &zu);
    err = hstaging_get_var(var_desc->name, var_desc->version, elem_size,
            xl, yl, zl, xu, yu, zu, data, NULL);
    if (err < 0) {
        uloga("%s(): failed to get var\n", __func__);
        return -1;
    }

    void* cas;
    const char *indopt = "<binning start=0 end=1000 nbins=100 scale=simple/>";
    char *colname = "var";
    int size_index = 0;
    cas = fb_build_index_double(data, num_double_elem, indopt, (void*)colname, &size_index); 

    // perform I/O
/*
    char index_fname[256];
    sprintf(index_fname, "fb_index_process%d.dat", t->rank);
    FILE *f = fopen(index_fname, "a");
    if (f == NULL) {
        uloga("%s(): failed to create file %s\n", __func__, index_fname);
    } else {
        char *data_buf = (char*)malloc(size_index);
        memset(data_buf, 0, size_index);
        size_t size = size_index;
        size_t offset = 0;
        size_t block_size = 512*1024; // 512KB
        size_t bytes;
        while (offset < size) {
            if ((size-offset) >= block_size) {
                bytes = block_size;
            } else {
                bytes = (size-offset);
            }
            fwrite(data_buf+offset, 1, bytes, f);
            offset += bytes;
        }
        fclose(f); 
        free(data_buf);
    } 
*/

    tm_end = MPI_Wtime();
    if (cas == 0) {
        uloga("%s(): fb_build_index_double failed\n", __func__);
    }
  
    MPI_Barrier(comm->comm);
    tm_end1 = MPI_Wtime();
 
    uloga("%s(): rank %d size_index %d build_index_time %lf\n", __func__,
        t->rank, size_index, tm_end-tm_st);
    if (t->rank == 0) {
        uloga("%s(): indexing data time %lf\n", __func__, tm_end1-tm_st);
    }

    free(data);
    return 0;
}

/*
int dummy_s3d_staging_parallel_job(MPI_Comm comm, enum hstaging_location_type loc_type)
{
    int err;
    int level, color;
    int mpi_rank, mpi_nproc;
    MPI_Comm_size(comm, &mpi_nproc);
    MPI_Comm_rank(comm, &mpi_rank);
    if ((mpi_nproc % BK_GROUP_BASIC_SIZE) != 0) {
        uloga("%s(): error size needs to be multiply of %d\n",
            __func__, BK_GROUP_BASIC_SIZE);
        return -1;
    }

    communicator_init(comm);
    level = 0;
    color = 1;
    comms[level].level = level;
    comms[level].color = color;
    comms[level].comm = comm;
    recursive_split_mpi_comm(level, color);

    register_task_function(1, task1);
    register_task_function(2, task2);
    register_task_function(3, task3);
    register_task_function(4, task4);
    register_task_function(5, task5);
    register_task_function(6, task6);
    register_task_function(7, task_viz_render);
    register_task_function(8, task_fb_indexing);

    hstaging_register_executor(mpi_nproc, mpi_rank);
    MPI_Barrier(comm);

	struct task_descriptor t;
	while (!hstaging_request_task(&t)) {
		hstaging_put_sync_all();
		err = exec_task_function(&t);	
		if (err < 0) {
			return err;
		}
	}

    hstaging_put_sync_all();
    communicator_free();

	return 0;
 err_out:
	return -1;
}
*/

/*
int dummy_sample_dag_workflow(MPI_Comm comm)
{
    int err;
    int mpi_rank, mpi_nproc;
    MPI_Comm_size(comm, &mpi_nproc);
    MPI_Comm_rank(comm, &mpi_rank);

    communicator_init(comm);

    int appid;
    for (appid = 1; appid < MAX_NUM_TASKS; appid++) {
        register_task_function(appid, dag_task);
    }
    int pool_id = 1;
    hstaging_register_executor(pool_id, mpi_nproc, mpi_rank);
    MPI_Barrier(comm);

	struct task_descriptor t;
	while (!hstaging_request_task(&t)) {
		hstaging_put_sync_all();
		err = exec_task_function(&t);	
		if (err < 0) {
			return err;
		}
	}

    hstaging_put_sync_all();
    communicator_free();

	return 0;
 err_out:
	return -1;
}
*/

int dummy_epsi_coupling_workflow(MPI_Comm comm)
{
    int err;
    int mpi_rank, mpi_nproc;
    MPI_Comm_size(comm, &mpi_nproc);
    MPI_Comm_rank(comm, &mpi_rank);

    communicator_init(comm);

    register_task_function(1, xgc1_task);
    register_task_function(2, xgca_task);

    int pool_id = 1;
    hstaging_register_executor(pool_id, mpi_nproc, mpi_rank);
    MPI_Barrier(comm);

    struct task_descriptor t;
    while (!hstaging_request_task(&t)) {
        hstaging_put_sync_all();
        err = exec_task_function(&t);
        if (err < 0) {
            return err;
        }
    }
    hstaging_put_sync_all();
    communicator_free();

    return 0;
}

int dummy_s3d_analysis_workflow(MPI_Comm comm)
{
    int err;
    int mpi_rank, mpi_nproc;
    MPI_Comm_size(comm, &mpi_nproc);
    MPI_Comm_rank(comm, &mpi_rank);

    communicator_init(comm);

    register_task_function(1, s3d_task);
    register_task_function(2, s3d_viz_task);
    register_task_function(3, s3d_stat_task); 
    register_task_function(4, s3d_topo_task);
    register_task_function(5, s3d_indexing_task);

    int pool_id = 1;
    hstaging_register_executor(pool_id, mpi_nproc, mpi_rank);
    MPI_Barrier(comm);

    struct task_descriptor t;
    while (!hstaging_request_task(&t)) {
        hstaging_put_sync_all();
        err = exec_task_function(&t);
        if (err < 0) {
            return err;
        }
    }
    hstaging_put_sync_all();
    communicator_free();

    return 0;
}

int dummy_dns_les_workflow(MPI_Comm comm)
{
    int err;
    int mpi_rank, mpi_nproc;
    MPI_Comm_size(comm, &mpi_nproc);
    MPI_Comm_rank(comm, &mpi_rank);

    communicator_init(comm);

    register_task_function(1, dns_task);
    register_task_function(2, les_task);

    int pool_id = 1;
    hstaging_register_executor(pool_id, mpi_nproc, mpi_rank);
    MPI_Barrier(comm);

    struct task_descriptor t;
    while (!hstaging_request_task(&t)) {
        hstaging_put_sync_all();
        err = exec_task_function(&t);
        if (err < 0) {
            return err;
        }
    }
    hstaging_put_sync_all();
    communicator_free();

    return 0;
}
