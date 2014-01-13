#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <mpi.h>


#include "gio_err.h"
#include "gio_io.h"
#include "gio_mem.h"
#include "gio_util.h"

#define OPT_LEN (32)
#define PATH_LEN (256)

#define GIO_LARGE_FILE

double get_dtime(void);
void usage(void);
void get_rank_path(char *mypath);
void get_coll_io_path(char *mypath, int comm_size);

int* create_io_data(int start_val);
void free_io_data(int *wdata);
int  validate_io_data(int *data, int val);

void do_sequential_read();
void do_sequential_write();
void do_experiment();


static struct option option_table[] = {
  {"e", required_argument, 0, 0},
  {"s", required_argument, 0, 0},
  {"f", required_argument, 0, 0},
  {"d", required_argument, 0, 0},
  {"m", required_argument, 0, 0},
  {0, 0, 0, 0}
};

struct perf_times {
  char*  name;
  double start;
  double end;
};

static struct perf_times ptimes[] = {
  {"total_time   ", 0, 0},     //0
  {"init_time    ", 0, 0},      //1
  {"open_time    ", 0, 0},      //2
  {"set_view_time", 0, 0},  //3
  {"io_time      ", 0, 0},     //4
  {"close_time   ", 0, 0}      //5
};

int myrank;
int world_comm_size;

/*Configurable value*/
int  expr_on = 0;
char expr[OPT_LEN];
int  scale_on = 0;
char scale[OPT_LEN];
int    data_size_on = 0;
size_t data_size = 0;
char target_path[PATH_LEN];
int  target_path_on = 0;
int  m_size_on = 0; /*M of NxM*/
int  m_size = 0;

/*Static value, which can not be changed*/
int max_striping_factor = 80; // if we use over 64 oss, deleting file operation hangs.
char striping_factor[8];
//char *striping_unit =   "67108864"; //  64MB
char *striping_unit =  "134217728";   // 128MB
//char *striping_unit =  "536870912"; // 512MB

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
      case 4:
	m_size = atoi(optarg);
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

  if (strcmp(expr, "pw") == 0) {
    if (m_size == 0) {
      usage();
      exit(EXIT_SUCCESS);
    }
  }

  do_experiment();

  MPI_Finalize(); 
  return 0;
}

void print_results()
{
  struct perf_times *gathered_ptimes = NULL;
  int i, j;

  /*Gather elapesd time */
  if (myrank == 0) {
    gathered_ptimes = (struct perf_times*)gio_malloc(sizeof(ptimes) * world_comm_size);
  }

  MPI_Gather(ptimes, sizeof(ptimes), MPI_BYTE, 
	     gathered_ptimes, sizeof(ptimes), MPI_BYTE,
	     0, MPI_COMM_WORLD);

  if (myrank == 0) {
    for (i = 0; i < world_comm_size; i++) {
      struct perf_times *output_ptimes = &gathered_ptimes[i * sizeof(ptimes)/sizeof(struct perf_times)];
      gio_print("-----------------------------------------------");
      gio_print("rank: %d", i);
      gio_print("        \tStart    \tEnd      \tElapsed");
      for (j = 0; j < sizeof(ptimes)/sizeof(struct perf_times); j++) {

	gio_print("%s\t%f\t%f\t%f", 
		  ptimes[j].name,
		  output_ptimes[j].start,
		  output_ptimes[j].end,
		    output_ptimes[j].end - output_ptimes[j].start
		  );
      }
    }
  }

  if (myrank == 0) {
    gio_free(gathered_ptimes);
  }
  return;
}





int get_sub_collective_io_comm(MPI_Comm *sub_comm)
{
  int sub_comm_size, sub_comm_color;

  /* Construct sub collective I/O communicater 
     according to group_count
   */
  if (world_comm_size % m_size != 0) {
    gio_err("world_comm_size:%d can not be divided by m_size:%d (%s:%s:%d)", 
	    world_comm_size, m_size, __FILE__, __func__, __LINE__);
  }
  sub_comm_size = world_comm_size / m_size;
  sub_comm_color = myrank / sub_comm_size;
  MPI_Comm_split(MPI_COMM_WORLD, sub_comm_color, myrank, sub_comm);
  return sub_comm_color;
}

int validate_io_data(int *data, int val)
{
  int i;
  int int_count;

  int_count = data_size / sizeof(int);
  for (i = 0; i < int_count; i++) {
    if (data[i] != val) {
      gio_err("data is not validated at index %d. Value:%d is expected, but is %d (%s:%s:%d)", 
	      i, val, data[i], __FILE__, __func__, __LINE__);
      return 0;
    }
    /* if (i % (int_count / 5) == 0) { */
    /*   gio_dbg("index:%d - value:%d == %d", i, data[i], val); */
    /* } */
  }  
  return 1;  
}

int* create_io_data(int val)
{
  int *wdata;
  int int_count;
  int i;

  if (data_size % sizeof(int) != 0) {
    gio_err("data_size:%d must be divided by integer size (%d bytes) (%s:%s:%d)", 
	    data_size, sizeof(int), __FILE__, __func__, __LINE__);
  }

  wdata = (int*)gio_malloc(data_size);  

  int_count = data_size / sizeof(int);
  for (i = 0; i < int_count; i++) {
    wdata[i] = val;
  }

  return wdata;
}

void free_io_data(int *wdata)
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
  int sub_comm_size, sub_rank, sub_comm_color;
  int striping_factor_int;
  int disp;
  int rc;
  int *buf;


  ptimes[0].start = MPI_Wtime();
  ptimes[1].start = MPI_Wtime();
  //  if (m_size == 1) {
  //    sub_write_comm = MPI_COMM_WORLD;
  //    sub_comm_color = 0;
  //  } else {
    sub_comm_color = get_sub_collective_io_comm(&sub_write_comm);  
    //  }

  /* Construct a datatype for distributing the input data across all
   * processes. */
  MPI_Type_contiguous(data_size / sizeof(int), MPI_INT, &contig);
  MPI_Type_commit(&contig);
  
  /* Set the stripe_count and stripe_size, that is, the striping_factor                                                                                                                                    
   * and striping_unit. Both keys and values for MPI_Info_set must be                                                                                                                                      
   * in the form of ascii strings. */
  MPI_Info_create(&info);
  striping_factor_int = max_striping_factor / m_size;
  if (striping_factor_int == 0) striping_factor_int = 1;
  sprintf(striping_factor, "%d", striping_factor_int);
  MPI_Info_set(info, "striping_factor", striping_factor);
  MPI_Info_set(info, "striping_unit", striping_unit);
  //  MPI_Info_set(info, "romio_cb_write", "enable");                                                                                                                                                       
  //  MPI_Info_set(info, "romio_cb_write", "disable");

  /* Get path to the target file of the communicator */
  MPI_Comm_size(sub_write_comm, &sub_comm_size);
  get_coll_io_path(coll_path, sub_comm_color);

  /* Delete the output file if it exists so that striping can be set                                                                                                                                          * on the output file. */
  //  rc = MPI_File_delete(coll_path, info);

  /* Create write data*/
  MPI_Comm_rank(sub_write_comm, &sub_rank);
  buf = create_io_data(sub_rank);
  /* if (sub_rank == 0) { */
  /*   rc = MPI_File_delete(coll_path, MPI_INFO_NULL); */
  /*   if (rc != MPI_SUCCESS) { */
  /*     gio_err("MPI_File_delete failed  (%s:%s:%d)", __FILE__, __func__, __LINE__); */
  /*   } */
  /* } */
  ptimes[1].end = MPI_Wtime();

  MPI_Barrier(MPI_COMM_WORLD);

  /* Open the file */
  //  gio_dbg("start ***********************");  
  ptimes[2].start = MPI_Wtime();
  rc = MPI_File_open(sub_write_comm, coll_path, 
		     MPI_MODE_WRONLY | MPI_MODE_CREATE, 
		     info, &fh);

  ptimes[2].end = MPI_Wtime();

  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_open failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }

  //  gio_dbg("start *********************** %d", sub_rank);  
  ptimes[3].start = MPI_Wtime();
  /* Set the file view for the output file. In this example, we will                                                                                                                                          * use the same contiguous datatype as we used for reading the data                                                                                                                                          * into local memory. A better example would be to write out just                                                                                                                                            * part of the data, say 4 contiguous elements followed by a gap of                                                                                                                                          * 4 elements, and repeated. */
  disp = sub_rank * data_size;
#ifdef GIO_LARGE_FILE
  int i;
  for (i = 0; i < sub_rank; i++) {
    MPI_File_seek(fh, data_size, MPI_SEEK_CUR);
  }
#else  
  MPI_File_set_view(fh, disp, contig, contig, "native", info);
#endif
  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_set_view failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }
  ptimes[3].end = MPI_Wtime();
  //  gio_dbg("end ***********************");  

  /* MPI Collective Write */
  ptimes[4].start = MPI_Wtime();
  rc = MPI_File_write_all(fh, buf, 1, contig, MPI_STATUS_IGNORE);
  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_set_view failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }
  ptimes[4].end = MPI_Wtime();

  /*Free data*/
  free_io_data(buf);

  /* Close Files */
  ptimes[5].start = MPI_Wtime();
  MPI_File_close(&fh);
  ptimes[5].end = MPI_Wtime();
  ptimes[0].end = MPI_Wtime();

  print_results();

  return;
}

void do_collective_read()
{
  MPI_Info info;
  MPI_Datatype contig;
  MPI_Comm sub_read_comm;
  MPI_File fh;
  char coll_path[PATH_LEN];
  int sub_comm_size, sub_rank, sub_comm_color;
  int disp;
  int rc;
  int *buf;

  ptimes[0].start = MPI_Wtime();
  ptimes[1].start = MPI_Wtime();
  sub_comm_color = get_sub_collective_io_comm(&sub_read_comm);  

  /* Construct a datatype for distributing the input data across all
   * processes. */
  MPI_Type_contiguous(data_size / sizeof(int), MPI_INT, &contig);
  MPI_Type_commit(&contig);
  
  /* Set the stripe_count and stripe_size, that is, the striping_factor                                                                                                                                    
   * and striping_unit. Both keys and values for MPI_Info_set must be                                                                                                                                      
   * in the form of ascii strings. */
  MPI_Info_create(&info);
  //  MPI_Info_set(info, "striping_factor", striping_factor);
  //  MPI_Info_set(info, "striping_unit", striping_unit);
  MPI_Info_set(info, "romio_cb_read", "enable");                                                                                                                                                       
  //  MPI_Info_set(info, "romio_cb_read", "disable");

  /* Get path to the target file of the communicator */
  MPI_Comm_size(sub_read_comm, &sub_comm_size);
  get_coll_io_path(coll_path, sub_comm_color);

  /* Delete the output file if it exists so that striping can be set                                                                                                                                          * on the output file. */
  //  rc = MPI_File_delete(coll_path, info);

  /* Create read data*/
  MPI_Comm_rank(sub_read_comm, &sub_rank);
  buf = create_io_data(-1);
  ptimes[1].end = MPI_Wtime();

  MPI_Barrier(MPI_COMM_WORLD);

  /* Open the file */
  ptimes[2].start = MPI_Wtime();
  rc = MPI_File_open(sub_read_comm, coll_path, 
		     MPI_MODE_RDONLY, 
		     info, &fh);
  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_open failed: %s  (%s:%s:%d)", coll_path, __FILE__, __func__, __LINE__);
  }
  ptimes[2].end   = MPI_Wtime();

  /* Set the file view for the output file. In this example, we will                                                                                                                                          * use the same contiguous datatype as we used for reading the data                                                                                                                                          * into local memory. A better example would be to read out just                                                                                                                                            * part of the data, say 4 contiguous elements followed by a gap of                                                                                                                                          * 4 elements, and repeated. */
  ptimes[3].start = MPI_Wtime();
#ifdef GIO_LARGE_FILE
  int i;
  for (i = 0; i < sub_rank; i++) {
    MPI_File_seek(fh, data_size, MPI_SEEK_CUR);
  }
#else  
  disp = sub_rank * data_size;
  MPI_File_set_view(fh, disp, contig, contig, "native", info);
#endif
  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_set_view failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }
  ptimes[3].end = MPI_Wtime();

  /* MPI Collective Read */
  ptimes[4].start = MPI_Wtime();
  rc = MPI_File_read_all(fh, buf, 1, contig, MPI_STATUS_IGNORE);
  if (rc != MPI_SUCCESS) {
    gio_err("MPI_File_set_view failed  (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }
  ptimes[4].end = MPI_Wtime();

  validate_io_data(buf, sub_rank);

  /*Free data*/
  free_io_data(buf);

  /* Close Files */
  ptimes[5].start = MPI_Wtime();
  MPI_File_close(&fh);
  ptimes[5].end = MPI_Wtime();
  ptimes[0].end = MPI_Wtime();

  print_results();

  return;
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
  sprintf(mypath, "%s/gio-file.%d.%d", target_path, myrank, getpid());
  return;
}

void get_coll_io_path(char *mypath, int comm_color)
{
  sprintf(mypath, "%s/gio-file.coll.%d.%d", target_path, comm_color, m_size);
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
  if (myrank == 0) {
    int sf = max_striping_factor / m_size;
    if (sf == 0) sf = 1;
    gio_print("===============================================");
    gio_print("Experiment          : %s", expr);
    gio_print("Scale               : %s", scale);
    gio_print("local_data_size     : %d", data_size);
    gio_print("Target path         : %s", target_path);
    gio_print("# of processes      : %d", world_comm_size);
    gio_print("# of files          : %d", m_size);
    gio_print("max_striping_factor : %d", max_striping_factor);
    gio_print("striping_factor     : %d", sf);
    gio_print("striping_unit       : %s", striping_unit);
  }
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



