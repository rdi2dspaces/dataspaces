# NB: this version of DataSpaces is unsupported and archived. Please find the supported version of DataSpaces [here](https://github.com/sci-ndp/dspaces)

Contents of this README file:
 * About DataSpaces
 * Building and Installing DataSpaces
 * DataSpaces APIs
 * Test Applications
 * FAQ


About DataSpaces
=================

This package contains the source code and two test applications for
DataSpaces run-time library, which is a flexible interaction and
coordination framework to support coupled applications.

DataSpaces is a distributed framework built on a dynamic set of nodes
of a HPC cluster, which implements a virtual shared-space abstraction
that can be accessed concurrently by all applications in a coupled
simulation workflow. The coupled applications can coordinate and
interact using DataSpaces by inserting and fetching scientific data
of interest through the distributed in-memory space.

DataSpaces provides a simple, yet powerful interface similar to the
tuple-space model, e.g., dspaces_put()/dspaces_get() to insert/retrieve data
objects into/from the virtual space.


Building and Installing DataSpaces
==================================

Please read the INSTALL file.


DataSpaces APIs
===============

Please read header files include/dataspaces.h and include/dimes_interface.h for
detailed API documentation. 

Test Applications
=================

We include two set of test code in the package. They are under tests directory.
The test is a simple workflow composed of two applications (a writer and a
reader), which interact through DataSpaces at runtime. The workflow works as
follow: the writer processes generates array of floating points number, obtain
the write lock, write it to the staging area, then release the write lock. The
reader processes first obtain the read lock, read data from the staging area, 
and release the read lock. This simple write/read data interaction can repeat
for a number of iterations defined by user.

The writer/reader programs are written in C and Fortran and are located in
C and Fortran directory respectively. The DataSpaces staging server code is
written in C and in C directory.

The test workflow include the following main source files:

        'tests/C/dataspaces_server.c': DataSpaces staging server
        'tests/C/test_writer.c' : writer application
        'tests/C/test_reader.c' : reader application
        'tests/Fortran/test_put.F90' : writer application
        'tests/Fortran/test_get.F90' : reader application


1. Building the example
------------------------

Under the tests directory (tests/C or tests/Fortran execute

        $ make

to create the following binary executables
'/tests/C/dataspaces_server',  '/tests/C/test_reader', 'tests/C/test_writer',
'tests/Fortran/test_put' and '/tests/Fortran/test_get'. Before running Fortran
test code, user needs to copy the staging server executable (dataspaces_server
) to Fortran directory. 


2. Preparing the configuration file for DataSpaces
---------------------------------------------------

User need to create a configuration file dataspaces.conf under C or Fortran
test directory. dataspaces.conf file should be edited according to the specific
requirements of the applications. A sample configuration file is included under
scripts/dataspaces.conf.

Please read the FAQ section on how to set the configuration parameters for
dataspaces.conf file.


3. How to run test workflow
---------------------------

In the scripts/fortran_test and scripts/c_test directory, we provide a number 
of PBS job script examples. These files should be modified according to
the specific workflow scenario and the system you plan to run DataSpaces.

The DataSpaces server executable has three command line options:

  --server, -s    number of server instance/process
  --cnodes, -c    number of client processes
  --conf, -f      path to the configuration file

In the test/Fortran directory, the test application executables are 'test_put'
and 'test_get'. Running the executables requires 8 arguments as arg1-arg8:

  arg1 : number of total application processes
  arg2 : number of dimensions for application data domain
  arg3 : number of processes in x direction (of the process layout)
  arg4 : number of processes in y direction (of the process layout)
  arg5 : number of processes in z direction (of the process layout)
  arg6 : block size per process in x direction (of the app data domain)
  arg7 : block size per process in y direction (of the app data domain)
  arg8 : block size per process in z direction (of the app data domain)
  arg9 : number of iterations

Here is an example:
  dataspaces_server -s 4 -c 72 
  test_put 64 3 4 4 4 256 256 256 50 
  test_get 8 3 4 2 1 256 512 1024 50 

In the test/C directory, the test application executables are 'test_writer'
and 'test_reader'. Running the executables requires the following arguments:
  test_writer transfer_method npapp ndim np_1 ... np_ndim sp_1 .. sp_ndim timestep appid elem_size num_vars
  test_reader transfer_method npapp ndim np_1 ... np_ndim sp_1 .. sp_ndim timestep appid elem_size num_vars

  transfer_method: data transfer method (DATASPACES or DIMES).
  npapp: number of total application processes.
  ndim: number of dimensions for application data domain.
  np_1~np_ndim: number of application processes in dimension 1~ndim (of the process layout).
  sp_1~sp_ndim: block size per process in dimension 1~ndim (of the app data domain).
  timestep: number of iteration.
  appid: application id.
  elem_size: Optional. Size of one element for the application array. Default value is 8 (bytes).
  num_vars: Optional. Number of array variables to be transferred between writer and reader application. Default value is 1.

Here is an example:
  dataspaces_server -s 4 -c 72
  test_writer DATASPACES 64 3 4 4 4 256 256 256 50 1
  test_reader DATASPACES 8 3 4 2 1 256 512 1024 50 2

Both Fortran and C tests are using the same dataspaces_server executable written in C.

Note: if you would like to write your own job script, please add command rm -f conf
srv.lck to your script in order to remove config files from previous run.

DataSpaces-as-a-Service (DSaaS):
================================

IMPORTANT change as of 1.7.0: 

The DataSpaces server now runs as a persistent service for the GNI and
Infiniband transport methods. Client applications may come and go through
the life of the server. The server will not remove data based upon the
disconnection of a client. The server no longer needs to know how many
client processes will connect, so the -c flag is now ignored for GNI and
Infiniband transports. 

Users should be aware that the server will continue to run, even if all 
clients have disconnected. A new API call has been created to allow a 
client to kill the server: dspaces_kill(). This should be used after 
dspaces_init(), but before dspaces_finalize(). Alternatively, a job 
script could kill the launch of the server externally after all client
applications have completed.

Hybrid Staging:
===============

Options have been added to support hybrid staging for read operations. When
enabled, DataSpaces will use shared memory communication to perform reads
when the data being read is already on the same node, either because the
reader rank is colocated with the server (DataSpaces case), or the writer
and reader ranks are colocated (DIMES case.)

See INSTALL for usage.

Metadata Support API:
=====================
New feature introduced 1.8.0:


FAQ
===

1. How to set configure parameters for dataspaces.conf
------------------------------------------------------

Running DataSpaces requires users to create a customized dataspaces.conf file.
The configuration file has the following parameters:

(1) ndim: number of dimensions for the default application data domain.
(2) dims: size of each dimension for the default application data domain.

DataSpaces needs the global dimension information of application data to build
the distributed indexing. Users can use the above two configuration parameters
to specify a default global dimension. For example, in a workflow where
applications exchange multiple 2D arrays with the same global dimension as
256x128, the parameters can be set as below.
ndim = 2
dims = 256,128

Note: For large values of ndim and dims, initialization of dataspaces_server will take significantly
more time. The only way to make sure dataspaces_clients to wait enough for the dataspaces_server 
initialization is to run the server and have an approximate estimate on the time clients should wait
for the server.

Since DataSpaces 1.4.0 release, user application can explicitly set the global
dimension for each variable using dspaces_define_gdim() and dimes_define_gdim()
APIs (see include/dataspaces.h and include/dimes_interface.h for detailed
documentation). In this case, the default global dimension is only used for
application variables whose global dimension is not explicitly defined. 

(3) max_versions: maximum number of versions of a data object to be cached
in DataSpaces servers.

DataSpaces servers can cache multiple versions of a data object in memory.
Specifically, the servers cache most recent 'max_version' versions of a data
object. When newer version is inserted into DataSpaces, older version of the
data object is freed in a FIFO manner. Version information is explicitly defined
by user application when calling dspaces_put() API.

Note: in current implementation, 'max_version' is only useful for DataSpaces
transport method. The value of 'max_version' does not affect DIMES transport
method.

(4) max_readers: maximum number of reader applications in the workflow.

(5) lock_type: type of DataSpaces locking service. Valid values: 1 - generic, 2 - custom.

Lock type 1 implements the exclusive lock. Application can acquire the lock by
calling dspaces_lock_on_write()/dspaces_lock_on_read(), as long as the lock is
not acquired by another application. 

Lock type 2 implements a customized lock that enforces writer/reader
synchronization. Writer application can always acquire the lock when calling
dspaces_lock_on_write() for the first time. In the subsequent calls to
dspaces_lock_on_write(), acquiring the lock requires all the reader applications
first call dspaces_unlock_on_read(). For reader applications, acquiring the lock
requires the writer application first call dspaces_unlock_on_write().

For example, the writer application calls the lock/unlock functions in the
following order:
    // Writer
    dspaces_lock_on_write("var1", NULL);
    ...
    dspaces_unlock_on_write("var1", NULL);

    dspaces_lock_on_write("var1", NULL);
    ...
    dspaces_unlock_on_write("var1", NULL);

And the reader application calls the lock/unlock functions in the following
order:
    // Reader
    dspaces_lock_on_read("var1", NULL);
    ...
    dspaces_unlock_on_read("var1", NULL);

Lock type 2 would ensure the applications acquire the lock in the following
order:
    writer acquires lock for "var1";
    reader acquires lock for "var1";
    writer acquires lock for "var1";

Lock type 3 is similar to lock type 1, but does not allow a read lock to
be acquired until after the first write lock is released.

2. Set the number of DataSpaces servers
---------------------------------------

Running DataSpaces requires users to specify the number of servers.

(1) Set the number of servers for DataSpaces transport method:
When DataSpaces transport method (e.g. dspaces_put()/dspaces_get()) is used,
application data is cached in the memory of DataSpaces server processes.
Two major factors need to be considered when choosing the number
of servers.

First, estimated total size of application data. Because all the application data
is cached in the DRAM of server processes, total data size would have to fit in
severs' memory aggregation. For example, the workflow applications write a total of 16GB
data in a single time step, and the max_version is set as 10. DataSpaces servers
need to cache an estimated size of 160GB data in memory. Assuming we run a single
DataSpaces server on one physical compute node (of HPC cluster), and each compute
nodes has about 8GB DRAM. As a result, we need to execute at least 10 DataSpaces
servers for this workflow.
 
Second, total number of application processes in the workflow. During the
execution of dspaces_put()/dspaces_get(), it involves a lot of messaging and data
communications between application processes and DataSpaces servers. As a
result, when the number of application processes is increased, it is recommended
to also increase the number of DataSpaces servers, which would help reduce the
messaging and data communications load on individual server. For example, if you
run 8 servers for 1024 application processes, it is better to run 128 servers
for 16384 application processes instead of keeping the number of servers
unchanged even if the total data size is fixed.

(2) Set the number of servers for DIMES transport method:
When DIMES transport methods (e.g. dimes_put()/dimes_get()) is used, application
data is directly cached in the application memory. DataSpaces server is used to
index the cached data objects, and locate data objects for application's
dimes_get() queries. In this case, the total size of application data does not
affect the number of servers. However, because the application processes still
need to interact with the servers, the second factor (above) still needs to be
considered.

For more information on installing DataSpaces and running the examples, or
if you have any other questions, please visit the following web page:
http://personal.cac.rutgers.edu/TASSL/projects/data/support.html
