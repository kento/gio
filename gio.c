#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/types.h>
#include <getopt.h>
#include <mpi.h>


#include "gio_err.h"
#include "gio_io.h"
#include "gio_mem.h"
#include "gio_util.h"

#define OPT_LEN (32)
#define PATH_LEN (256)


double get_dtime(void);
void usage(void);
void get_rank_path(char *mypath);
void get_coll_io_path(char *mypath, int comm_size);
int* create_write_data(int start_val);
void free_write_data(int *wdata);

void do_sequential_read();
void do_sequential_write();
void do_experiment();



static struct option option_table[] = {
  {"e", required_argument, 0, 0},
  {"s", required_argument, 0, 0},
  {"f", required_argument, 0, 0},
  {"d", required_argument, 0, 0},
  {0, 0, 0, 0}
};

int myrank;
int world_comm_size;

int  expr_on = 0;
char expr[OPT_LEN];
int  scale_on = 0;
char scale[OPT_LEN];
int    data_size_on = 0;
size_t data_size = 0;
char target_path[PATH_LEN];
int  target_path_on = 0;


int main(int argc,char *argv[])
{
  int c;
  int option_index;

  MPI_Init(&argc, &argv); 
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank); 
  MPI_Comm_size (MPI_COMM_WORLD, &world_comm_size); 

  gio_err_init(myrank);

  do {
    c = getopt_long_only(argc, argv, "+", option_table, &option_index);
    switch (c) {
    case '?':
    case ':':
      usage();
      exit(EXIT_FAILURE);
      break;
    case EOF:
      break;
    case 0:
      switch (option_index) {
      case 0:
	strcpy(expr, optarg);
	expr_on = 1;
	break;
      case 1:
	strcpy(scale, optarg);
	scale_on = 1;
	break;
      case 2:
	data_size = atoi(optarg);
	data_size_on = 1;
	break;
      case 3:
	strcpy(target_path, optarg);
	target_path_on = 1;
	break;
      default:
	gio_dbg("Unknown option\n");
	usage();
	exit(EXIT_FAILURE);
	break;
      }
      break;
    default:
      gio_dbg("Unknown option\n");
      usage();
      exit(EXIT_FAILURE);
      break;
    }
  } while (c != EOF);

  if (!expr_on || !scale_on || !data_size_on || !target_path_on) {
    usage();
    exit(EXIT_SUCCESS);
  }

  do_experiment();

  MPI_Finalize(); 
  return 0;
}

char *striping_factor = "4";
char *striping_unit = "1048576";
long local_size = 1048576L;
int sub_comm_count = 2;

void get_sub_collective_io_comm(MPI_Comm *sub_comm)
{
  int sub_comm_size, sub_comm_color;

  /* Construct sub collective I/O communicater 
     according to group_count
   */
  if (world_comm_size % sub_comm_count != 0) {
    gio_err("world_comm_size:%d can not be divided by sub_comm_count:%d (%s:%s:%d)", 
	    world_comm_size, sub_comm_count, __FILE__, __func__, __LINE__);
  }
  sub_comm_size = world_comm_size / sub_comm_count;
  sub_comm_color = myrank / sub_comm_size;
  MPI_Comm_split(MPI_COMM_WORLD, sub_comm_color, myrank, sub_comm);
  return;
}

int* create_write_data(int val)
{
  int *wdata;
  int int_count;
  int i;

  wdata = (int*)gio_malloc(local_size);  

  int_count = local_size / sizeof(int);
  for (i = 0; i < int_count; i++) {
    wdata[i] = val;
  }

  return wdata;
}

void free_write_data(int *wdata)
{
  gio_free(wdata);
  return;
}

void do_collective_write()
{
  MPI_Info info;
  MPI_Datatype contig;
  MPI_Comm sub_write_comm;
  MPI_File fh;
  char coll_path[PATH_LEN];
  int sub_comm_size = 0, sub_rank;
  int disp;
  int rc;
  int *buf;

  get_sub_collective_io_comm(&sub_write_comm);  

  /* Construct a datatype for distributing the input data across all
   * processes. */
  MPI_Type_contiguous(local_size / sizeof(int), MPI_INT, &contig);
  MPI_Type_commit(&contig);
  
  /* Set the stripe_count and stripe_size, that is, the striping_factor                                                                                                                                    
   * and striping_unit. Both keys and values for MPI_Info_set must be                                                                                                                                      
   * in the form of ascii strings. */
  MPI_Info_create(&info);
  MPI_Info_set(info, "striping_factor", striping_factor);
  MPI_Info_set(info, "striping_unit", striping_unit);
  MPI_Info_set(info, "romio_cb_write", "enable");                                                                                                                                                       
  //  MPI_Info_set(info, "romio_cb_write", "disable");

  /* Get path to the target file of the communicator */
  MPI_Comm_size(sub_write_comm, &sub_comm_size);
  get_coll_io_path(coll_path, sub_comm_size);

  /* Delete the output file if it exists so that striping can be set                                                                                                                                          * on the output file. */
  rc = MPI_File_delete(coll_path, info);

  /* Create write data*/
  MPI_Comm_rank(sub_write_comm, &sub_rank);
  buf = create_write_data(sub_rank * sub_comm_size);


  MPI_Barrier(MPI_COMM_WORLD);

  /* Open the file */
  rc = MPI_File_open(sub_write_comm, coll_path, 
		     MPI_MODE_WRONLY | MPI_MODE_CREATE, 
		     info, &fh);
  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_open failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }
  
  /* Set the file view for the output file. In this example, we will                                                                                                                                          * use the same contiguous datatype as we used for reading the data                                                                                                                                         * into local memory. A better example would be to write out just                                                                                                                                           * part of the data, say 4 contiguous elements followed by a gap of                                                                                                                                         * 4 elements, and repeated. */
  disp = myrank * local_size * sizeof(int);
  MPI_File_set_view(fh, disp, contig, contig, "native", info);
  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_set_view failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }

  /* MPI Collective Write */
  rc = MPI_File_write_all(fh, buf, 1, contig, MPI_STATUS_IGNORE);
  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_set_view failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }

  /*Free data*/
  free_write_data(buf);

  /* Close Files */
  MPI_File_close(&fh);
  return;
}

void do_collective_read()
{
}


void do_sequential_write()
{
  int fd;
  char *addr;
  size_t wsize;
  char mypath[PATH_LEN];

  if (myrank == 0) {
    gio_dbg("Write: scale: %s, size: %lu", scale, data_size);
  }

  get_rank_path(mypath);

  addr = (char*)gio_malloc(data_size);  
  
  fd = gio_open(mypath, O_WRONLY | O_CREAT, 0);
  if (fd < 0) {
    gio_err("File open failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }

  wsize = gio_write(mypath, fd, addr, data_size);
  if (wsize != data_size) {
    gio_err("Inputu wirte size is %f, but only %f bytes are written (%s:%s:%d)", data_size, wsize,__FILE__, __func__, __LINE__);
  }

  gio_close(mypath, fd);
  return;
}

void get_rank_path(char *mypath)
{
  sprintf(mypath, "%s/gio.%d.%d", target_path, myrank, getpid());
  return;
}

void get_coll_io_path(char *mypath, int comm_size)
{
  sprintf(mypath, "%s/gio.coll.%d.%d", target_path, comm_size, myrank);
  return;
}



void do_sequential_read()
{
  int fd;
  char *addr;
  size_t wsize;
  char mypath[PATH_LEN];

  if (myrank == 0) {
    gio_dbg("Reade: scale: %s, size: %lu", scale, data_size);
  }

  get_rank_path(mypath);

  addr = (char*)gio_malloc(data_size);  
  
  fd = gio_open(mypath, O_WRONLY | O_CREAT, 0);
  if (fd < 0) {
    gio_err("File open failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }

  wsize = gio_write(mypath, fd, addr, data_size);
  if (wsize != data_size) {
    gio_err("Inputu wirte size is %f, but only %f bytes are written (%s:%s:%d)", data_size, wsize,__FILE__, __func__, __LINE__);
  }

  gio_close(mypath, fd);
  return;

}

void do_experiment()
{

  MPI_Barrier(MPI_COMM_WORLD);
  if (strcmp(expr, "sw") == 0) {
    do_sequential_write();
  } else if (strcmp(expr, "sr") == 0) {
    do_sequential_read();
  } else if (strcmp(expr, "pw") == 0) {
    do_collective_write();
  } else if (strcmp(expr, "pr") == 0) {
    do_collective_read();
  } else {
    usage();
    exit(EXIT_SUCCESS);
  }
  return;
}

void usage()
{
  if (myrank == 0) {
    fprintf(stderr, "usage: gio -e [sw|sr|pw|pr] -s [s|w] -f size -d directory\n");
    fprintf(stderr, "Where:\n");
    fprintf(stderr, "\t-e       =>" 
	    " Experiment type: (sw/sr:sequencial write/read, "
                               "pw/pr:collective write/read with MPI-IO,)\n"
	    );
    fprintf(stderr, "\t-s       => " 
	    "s:strong scale, w:weak scale\n");
    fprintf(stderr, "\t-f       => " 
	    "d is data size (bytes)\n");
    fprintf(stderr, "\t-d       => " 
	    "target directory\n");
    fprintf(stderr, "\n");
  }
}



/* int main2(int argc,char *argv[]) */
/* { */
/*   int myid, world_size; */
/*   void *data; */
/*   char path[256]; */
/*   long size = 1 * 1024 * 1024 * 1024; */
/*   int fd; */

/*   double s,e; */

/*   MPI_Status stat; */
/*   MPI_Request req1, req2; */

/*   MPI_Init(&argc, &argv); */
/*   MPI_Comm_rank(MPI_COMM_WORLD, &myid); */
/*   MPI_Comm_size (MPI_COMM_WORLD, &world_size); */

/*   data = malloc(size); */
/*   memset(data, 1, size); */
/*   sprintf(path, "/work0/t2g-ppc-internal/11D37048/mpi_write/%d", myid); */
/*   //  sprintf(path, "/data0/t2g-ppc-internal/11D37048/%d", myid); */

/*   MPI_Barrier (MPI_COMM_WORLD); */

/*   s = get_dtime(); */
/*   dump(path, data, size); */
/*   MPI_Barrier (MPI_COMM_WORLD); */
/*   e = get_dtime(); */

/*   if (myid == 0) { */
/*     fprintf(stderr, "%d\t%lu\t%f\t%f\n", world_size, size * world_size, e - s, (size * world_size)/ (e - s)); */
/*   } */

/*   MPI_Finalize(); */
/*   return 0; */
/* } */



