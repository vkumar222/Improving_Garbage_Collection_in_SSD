#include <sys/stat.h>
#include <sys/types.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "ssd.h"
#include "raid.h"

int main( int argc, char *argv[] ) 
{ 
    unsigned int err;
    struct user_args *uargs;

    uargs=(struct user_args*) malloc(sizeof(struct user_args));
    alloc_assert(uargs,"user args");
    memset(uargs,0, sizeof(struct user_args));

    err = parse_user_args(argc, argv, uargs);
    if (err == -1) {
        display_help();
        return 0;
    }

    display_title();

    if (uargs->is_raid) {
        simulate_raid(uargs);
    } else {
        simulate_ssd(uargs);
    }

    free(uargs);
    printf("\nThe simulation is completed! \n");

    return 0;
}

// simulate_ssd is the main function to initialize and simulate a single ssd device
void simulate_ssd(struct user_args* uargs) 
{
    struct ssd_info *ssd;
    
    ssd=(struct ssd_info*)malloc(sizeof(struct ssd_info));
    alloc_assert(ssd,"ssd");
    memset(ssd,0, sizeof(struct ssd_info));

    ssd=initialize_ssd(ssd, uargs);
    // activate syncgc
    // ssd->is_gcsync = 1;
    // ssd->ndisk = 4;
    // ssd->diskid = 0;
    // ssd->gc_time_window = 100 * 1000000;
    printf("finish initialize ssd\n");

    ssd=initiation(ssd);
    printf("finish ssd initiation (prepare inner structure)\n");

    ssd=make_aged(ssd);
    ssd=pre_process_page(ssd);

    display_freepage(ssd);
    display_simulation_intro(ssd);
    ssd=simulate(ssd);

    // display_state(ssd);
    statistic_output(ssd);
    close_file(ssd);

    free(ssd);
}

// parse_user_args function parses optional and required options and arguments
// for ssdsim, the parsed arguments can be accessed through uargs.
// This function will return -1 if an error occurrs.
int parse_user_args(int argc, char *argv[], struct user_args* uargs) {
    char **positionals;
    int raidtype = -1;
    int ndisk = 0, diskid = 0;
    int64_t gc_time_window = 0;

    static struct option long_options[] = {
        {"raid0", no_argument, 0, '0'},
        {"raid5", no_argument, 0, '5'},
        {"gcsync", no_argument, 0, 's'},
        {"gclock", no_argument, 0, 'l'},
        {"gcdefer", no_argument, 0, 'd'},
        {"ndisk", required_argument, 0, 'n'},

        {"timestamp", required_argument, 0, 't'},       // simulation timestamp, for logging purpose
        {"diskid", required_argument, 0, 'i'},          // for gcsync purpose
        {"gc_time_window", required_argument, 0, 'g'},  // for gcsync purpose, in ns
        {"parameter", required_argument, 0, 'p'},       // parameter file
        {0, 0, 0, 0}
    };
    
    // Parsing program options
    int long_index = 0;
    int opt = 0;
    while ((opt = getopt_long(argc, argv,"05n:", long_options, &long_index )) != -1) {
        switch (opt) {
            case '5':
                if (uargs->is_raid == 1) {
                    printf("Error! only require one type of RAID! you have specified the RAID type previously\n");
                    return -1;
                }
                uargs->is_raid = 1;
                uargs->raid_type = RAID_5;
                break;
            case '0':
                if (uargs->is_raid == 1) {
                    printf("Error! only require one type of RAID! you have specified the RAID type previously\n");
                    return -1;
                }
                uargs->is_raid = 1;
                uargs->raid_type = RAID_0;
                break;
            case 'l':
                uargs->is_gclock = 1;
                break;
            case 'd':
                uargs->is_gcdefer = 1;
                break;    
            case 's':
                uargs->is_gcsync = 1;
                break;
            case 'n':
                ndisk = atoi(optarg);
                if (ndisk == 0) {
                    printf("Error! wrong number of disk!\n");
                    return -1;
                }
                uargs->num_disk = ndisk;
                break;
            case 't':
                strcpy(uargs->simulation_timestamp, optarg);
                break;
            case 'i':
                diskid = atoi(optarg);
                if (diskid < 0) {
                    printf("Error! wrong diskid, it must be >= 0, but get %d!\n", diskid);
                    return -1;
                }
                uargs->diskid = diskid;
                break;
            case 'g':
                gc_time_window = atoll(optarg);
                if (gc_time_window < 0) {
                    printf("Error! wrong gc_time_window, it must be > 0, but get %lld!\n", gc_time_window);
                    return -1;
                }
                uargs->gc_time_window = gc_time_window;
                break;
            case 'p':
                strcpy(uargs->parameter_filename, optarg);
                break;
            default:
                printf("Error! parse arguments failed.\n");
                return -1;
        }
    }

    // Parsing tracefile
    if (optind == argc) {
        printf("Error! require tracefile to run simulation\n");
        return -1;
    }
    strcpy(uargs->trace_filename, argv[optind]);

    // Additional constraints
    if (uargs->is_raid && uargs->num_disk == 0) {
        printf("Error! RAID simulation requires number of disk (--ndisk)\n");
        return -1;
    }
    if (uargs->raid_type == RAID_5 && uargs->num_disk < 3) {
        printf("Error! RAID 5 simulation needs at least 3 disks\n");
        return -1;
    }
    if (uargs->is_raid && uargs->num_disk < 2) {
        printf("Error! RAID simulation needs at least 2 disks\n");
        return -1;
    }
    if (uargs->is_gcsync + uargs->is_gclock + uargs->is_gcdefer > 1) {
        printf("Error! multiple gc scheduling algorithm activated!\n");
        return -1;
    }
    if (uargs->is_gcsync && !uargs->gc_time_window && !uargs->num_disk) {
        printf("Error! GCSync mode need ndisk, diskid, and gc_time_window!\n");
        return -1;
    }


    return 0;
}

// initialize_ssd function initializes ssd struct based on user arguments and also default value.
// the most important arguments to be initialized is the tracefile and also
// ssd parameter config file. This function also prepare all log file to store information about single ssd simulation
struct ssd_info *initialize_ssd(struct ssd_info* ssd, struct user_args* uargs) {
    int i;
    char *opt;
    char *current_time;
    char logdir[30];
    char logdirname[60];

    // Prepare log directory for this ssd
    current_time = (char*) malloc(sizeof(char)*16);
    if (strlen(uargs->simulation_timestamp) != 0) {
        strcpy(current_time, uargs->simulation_timestamp);
    } else {
        get_current_time(current_time);
    }
    strcpy(logdir, "raw/");
    strcat(logdir, current_time);
    if (0 != mkdir(logdir,0777)) {
        printf("When executing: mkdir(\"%s\")\n", logdir);
        perror("mkdir");
        exit(1);
    }
    strcat(logdir, "/");

    // Assign default value
    strcpy(logdirname, logdir); strcat(logdirname, "ex.out");
    strcpy(ssd->outputfilename, logdirname);
    strcpy(logdirname, logdir); strcat(logdirname, "statistic10.dat");
    strcpy(ssd->statisticfilename, logdirname);
    strcpy(logdirname, logdir); strcat(logdirname, "statistic2.dat");
    strcpy(ssd->statisticfilename2, logdirname);
    strcpy(logdirname, logdir); strcat(logdirname, "io.dat");
    strcpy(ssd->outfile_io_name, logdirname);
    strcpy(logdirname, logdir); strcat(logdirname, "io_write.dat");
    strcpy(ssd->outfile_io_write_name, logdirname);
    strcpy(logdirname, logdir); strcat(logdirname, "io_read.dat");
    strcpy(ssd->outfile_io_read_name, logdirname);
    strcpy(logdirname, logdir); strcat(logdirname, "gc.dat");
    strcpy(ssd->outfile_gc_name, logdirname);

    // Assign ssd parameter config file
    if (strlen(uargs->parameter_filename) == 0)
        strncpy(ssd->parameterfilename,"page.parameters",16);
    else
        strcpy(ssd->parameterfilename, uargs->parameter_filename);

    // Assign tracefilename
    strcpy(ssd->tracefilename, uargs->trace_filename);
    
    // Assign all var related to GCSync
    if (uargs->is_gcsync) {
        ssd->ndisk = uargs->num_disk;
        ssd->diskid = uargs->diskid;
        ssd->is_gcsync = 1;
        ssd->gc_time_window = uargs->gc_time_window;
    }

    // Assign all var related to GCLock
    if (uargs->is_gclock) {
        ssd->ndisk = uargs->num_disk;
        ssd->diskid = uargs->diskid;
        ssd->is_gclock = 1;
    }

    // Assign all var related to GCDefer
    if (uargs->is_gcdefer) {
        ssd->ndisk = uargs->num_disk;
        ssd->diskid = uargs->diskid;
        ssd->is_gcdefer = 1;
    }

    free(current_time);
    return ssd;
}

/******************simulate() *********************************************************************
 *simulate()是核心处理函数，主要实现的功能包括
 *1,从trace文件中获取一条请求，挂到ssd->request
 *2，根据ssd是否有dram分别处理读出来的请求，把这些请求处理成为读写子请求，挂到ssd->channel或者ssd上
 *3，按照事件的先后来处理这些读写子请求。
 *4，输出每条请求的子请求都处理完后的相关信息到outputfile文件中
 **************************************************************************************************/
struct ssd_info *simulate(struct ssd_info *ssd)
{
    int flag=1,flag1=0;
    double output_step=0;
    unsigned int a=0,b=0;

    ssd->tracefile = fopen(ssd->tracefilename,"r");
    if(ssd->tracefile == NULL) {
        printf("the trace file can't open\n");
        return NULL;
    }

    fprintf(ssd->outputfile,"      arrive           lsn     size ope     begin time    response time    process time\n");	
    fflush(ssd->outputfile);

    printf("GC processes log: page/plane = %u\n", ssd->parameter->page_block*ssd->parameter->block_plane);
    printf("cnl \tchp \tdie \tpln\tfrpg(%%)\t mvd_pg\t\tstart(ns)\t  end(ns)\t time(ns)\n");

    while(flag!=100)      
    {

        // Interface layer
        flag=get_requests(ssd);

        // Buffer layer
        if(flag == 1)
        {   
            if (ssd->parameter->dram_capacity!=0)
            {
                buffer_management(ssd);  
                distribute(ssd); 
            } 
            else
            {
                no_buffer_distribute(ssd);
            }		
        }

        // FTL+FCL+Flash layer
        process(ssd);
        trace_output(ssd);
        init_gc(ssd);

        // reach end of tracefile, all request already processed
        if(flag == 0 && ssd->request_queue == NULL)
            flag = 100;
    }

    fclose(ssd->tracefile);
    return ssd;
}



/********    get_request    ******************************************************
 *	1.get requests that arrived already
 *	2.add those request node to ssd->reuqest_queue
 *	return	0: reach the end of the trace
 *			-1: no request has been added
 *			1: add one request to list
 *SSD模拟器有三种驱动方式:时钟驱动(精确，太慢) 事件驱动(本程序采用) trace驱动()，
 *两种方式推进事件：channel/chip状态改变、trace文件请求达到。
 *channel/chip状态改变和trace文件请求到达是散布在时间轴上的点，每次从当前状态到达
 *下一个状态都要到达最近的一个状态，每到达一个点执行一次process
 ********************************************************************************/
int get_requests(struct ssd_info *ssd)  
{  
    char buffer[200];
    unsigned int lsn=0;
    int device,  size, ope,large_lsn, i = 0,j=0;
    struct request *request1;
    int flag = 1;
    long filepoint; 
    int64_t time_t = 0;
    int64_t nearest_event_time;    

#ifdef DEBUG
    printf("enter get_requests,  current time:%lld\n",ssd->current_time);
#endif

    // If not EOF, try to add new request
    if(!feof(ssd->tracefile)) {
        filepoint = ftell(ssd->tracefile);
        fgets(buffer, 200, ssd->tracefile);
        sscanf(buffer,"%lld %d %d %d %d",&time_t,&device,&lsn,&size,&ope);

        if (filepoint == 0) {
            ssd->simulation_start_time = time_t;
        }

    // If EOF, continue to process the request queue until empty
    } else {
        nearest_event_time=find_nearest_event(ssd);
        ssd->current_time=nearest_event_time;
        ssd->simulation_end_time = ssd->current_time;
        return 0;
    }

    if ((device<0)&&(lsn<0)&&(size<0)&&(ope<0)) {
        printf("Error! wrong io request from trace file\n");
        return 100;
    }

    if (lsn<ssd->min_lsn) 
        ssd->min_lsn=lsn;
    if (lsn>ssd->max_lsn)
        ssd->max_lsn=lsn;
    /******************************************************************************************************
     *上层文件系统发送给SSD的任何读写命令包括两个部分（LSN，size） LSN是逻辑扇区号，对于文件系统而言，它所看到的存
     *储空间是一个线性的连续空间。例如，读请求（260，6）表示的是需要读取从扇区号为260的逻辑扇区开始，总共6个扇区。
     *large_lsn: channel下面有多少个subpage，即多少个sector。overprovide系数：SSD中并不是所有的空间都可以给用户使用，
     *比如32G的SSD可能有10%的空间保留下来留作他用，所以乘以1-provide
     ***********************************************************************************************************/
    large_lsn=(int)((ssd->parameter->subpage_page*ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)*(1-ssd->parameter->overprovide));
    lsn = lsn%large_lsn;

    nearest_event_time=find_nearest_event(ssd);

    if (nearest_event_time==MAX_INT64)
    {
        ssd->current_time=time_t;           
    }
    else
    {
        if(nearest_event_time<time_t)
        {
            /*******************************************************************************
             *回滚，即如果没有把time_t赋给ssd->current_time，则trace文件已读的一条记录回滚
             *filepoint记录了执行fgets之前的文件指针位置，回滚到文件头+filepoint处
             *int fseek(FILE *stream, long offset, int fromwhere);函数设置文件指针stream的位置。
             *如果执行成功，stream将指向以fromwhere（偏移起始位置：文件头0，当前位置1，文件尾2）为基准，
             *偏移offset（指针偏移量）个字节的位置。如果执行失败(比如offset超过文件自身大小)，则不改变stream指向的位置。
             *文本文件只能采用文件头0的定位方式，本程序中打开文件方式是"r":以只读方式打开文本文件	
             **********************************************************************************/
            fseek(ssd->tracefile,filepoint,0); 
            if(ssd->current_time<=nearest_event_time)
                ssd->current_time=nearest_event_time;
            return -1;
        }
        else // nearest_event_time >= time_t
        {
            if (ssd->request_queue_length>=ssd->parameter->queue_length)
            {
                fseek(ssd->tracefile,filepoint,0);
                ssd->current_time=nearest_event_time;
                return -1;
            } 
            else
            {
                ssd->current_time=time_t;
            }
        }
    }

    if(time_t < 0)
    {
        printf("error!\n");
        while(1){}
    }

    if(feof(ssd->tracefile))
    {
        request1=NULL;
        return 0;
    }

    request1 = (struct request*)malloc(sizeof(struct request));
    alloc_assert(request1,"request");
    memset(request1,0, sizeof(struct request));

    request1->time = time_t;
    request1->lsn = lsn;
    request1->size = size;
    request1->operation = ope;	
    request1->begin_time = time_t;
    request1->response_time = 0;	
    request1->energy_consumption = 0;	
    request1->next_node = NULL;
    request1->distri_flag = 0;              // indicate whether this request has been distributed already
    request1->subs = NULL;
    request1->need_distr_flag = NULL;
    request1->complete_lsn_count=0;         //record the count of lsn served by buffer
    filepoint = ftell(ssd->tracefile);		// set the file point

    if(ssd->request_queue == NULL)          //The queue is empty
    {
        ssd->request_queue = request1;
        ssd->request_tail = request1;
        ssd->request_queue_length++;
    }
    else
    {			
        (ssd->request_tail)->next_node = request1;	
        ssd->request_tail = request1;			
        ssd->request_queue_length++;
    }

    if (request1->operation==READ)             //计算平均请求大小 1为读 0为写
    {
        ssd->ave_read_size=(ssd->ave_read_size*ssd->read_request_count+request1->size)/(ssd->read_request_count+1);
        ssd->read_request_size+=request1->size;
    } 
    else
    {
        ssd->ave_write_size=(ssd->ave_write_size*ssd->write_request_count+request1->size)/(ssd->write_request_count+1);
        ssd->write_request_size+=request1->size;
    }


    filepoint = ftell(ssd->tracefile);	
    fgets(buffer, 200, ssd->tracefile);    //寻找下一条请求的到达时间
    sscanf(buffer,"%lld %d %d %d %d",&time_t,&device,&lsn,&size,&ope);
    ssd->next_request_time=time_t;
    fseek(ssd->tracefile,filepoint,0);

    return 1;
}

/**********************************************************************************************************************************************
 *首先buffer是个写buffer，就是为写请求服务的，因为读flash的时间tR为20us，写flash的时间tprog为200us，所以为写服务更能节省时间
 *  读操作：如果命中了buffer，从buffer读，不占用channel的I/O总线，没有命中buffer，从flash读，占用channel的I/O总线，但是不进buffer了
 *  写操作：首先request分成sub_request子请求，如果是动态分配，sub_request挂到ssd->sub_request上，因为不知道要先挂到哪个channel的sub_request上
 *          如果是静态分配则sub_request挂到channel的sub_request链上,同时不管动态分配还是静态分配sub_request都要挂到request的sub_request链上
 *		   因为每处理完一个request，都要在traceoutput文件中输出关于这个request的信息。处理完一个sub_request,就将其从channel的sub_request链
 *		   或ssd的sub_request链上摘除，但是在traceoutput文件输出一条后再清空request的sub_request链。
 *		   sub_request命中buffer则在buffer里面写就行了，并且将该sub_page提到buffer链头(LRU)，若没有命中且buffer满，则先将buffer链尾的sub_request
 *		   写入flash(这会产生一个sub_request写请求，挂到这个请求request的sub_request链上，同时视动态分配还是静态分配挂到channel或ssd的
 *		   sub_request链上),在将要写的sub_page写入buffer链头
 * Read operation: If you hit the buffer, read from the buffer, do not occupy the channel I / O bus, do not hit the buffer, read from the flash, occupy the channel I / O bus, but do not enter the buffer
 ***********************************************************************************************************************************************/
struct ssd_info *buffer_management(struct ssd_info *ssd)
{   
    unsigned int j,lsn,lpn,last_lpn,first_lpn,index,complete_flag=0, state,full_page;
    unsigned int flag=0,need_distb_flag,lsn_flag,flag1=1,active_region_flag=0;           
    struct request *new_request;
    struct buffer_group *buffer_node,key;
    unsigned int mask=0,offset1=0,offset2=0;

#ifdef DEBUG
    printf("enter buffer_management,  current time:%lld\n",ssd->current_time);
#endif
    ssd->dram->current_time=ssd->current_time;
    full_page=~(0xffffffff<<ssd->parameter->subpage_page);

    new_request=ssd->request_tail;
    lsn=new_request->lsn;
    lpn=new_request->lsn/ssd->parameter->subpage_page;
    last_lpn=(new_request->lsn+new_request->size-1)/ssd->parameter->subpage_page;
    first_lpn=new_request->lsn/ssd->parameter->subpage_page;

    new_request->need_distr_flag=(unsigned int*)malloc(sizeof(unsigned int)*((last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32+1));
    alloc_assert(new_request->need_distr_flag,"new_request->need_distr_flag");
    memset(new_request->need_distr_flag, 0, sizeof(unsigned int)*((last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32+1));

    if(new_request->operation==READ) 
    {	
        while(lpn<=last_lpn)      		
        {
            /************************************************************************************************
             *need_distb_flag表示是否需要执行distribution函数，1表示需要执行，buffer中没有，0表示不需要执行
             *即1表示需要分发，0表示不需要分发，对应点初始全部赋为1
             *************************************************************************************************/
            need_distb_flag=full_page;   
            key.group=lpn;
            buffer_node= (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);		// buffer node 

            while((buffer_node!=NULL)&&(lsn<(lpn+1)*ssd->parameter->subpage_page)&&(lsn<=(new_request->lsn+new_request->size-1)))
            {
                lsn_flag=full_page;
                mask=1 << (lsn%ssd->parameter->subpage_page);
                if(mask>255) // 4KB page
                {
                    printf("the subpage number is larger than 8!add some cases");
                    getchar(); 		   
                }
                else if((buffer_node->stored & mask)==mask)
                {
                    flag=1;
                    lsn_flag=lsn_flag&(~mask);
                }

                if(flag==1)				
                {	//如果该buffer节点不在buffer的队首，需要将这个节点提到队首，实现了LRU算法，这个是一个双向队列。		       		
                    if(ssd->dram->buffer->buffer_head!=buffer_node)     
                    {		
                        if(ssd->dram->buffer->buffer_tail==buffer_node)								
                        {			
                            buffer_node->LRU_link_pre->LRU_link_next=NULL;					
                            ssd->dram->buffer->buffer_tail=buffer_node->LRU_link_pre;							
                        }				
                        else								
                        {				
                            buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;				
                            buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;								
                        }								
                        buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;
                        ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
                        buffer_node->LRU_link_pre=NULL;			
                        ssd->dram->buffer->buffer_head=buffer_node;													
                    }						
                    ssd->dram->buffer->read_hit++;					
                    new_request->complete_lsn_count++;											
                }		
                else if(flag==0)
                {
                    ssd->dram->buffer->read_miss_hit++;
                }

                need_distb_flag=need_distb_flag&lsn_flag;

                flag=0;		
                lsn++;						
            }	

            index=(lpn-first_lpn)/(32/ssd->parameter->subpage_page); 			
            new_request->need_distr_flag[index]=new_request->need_distr_flag[index]|(need_distb_flag<<(((lpn-first_lpn)%(32/ssd->parameter->subpage_page))*ssd->parameter->subpage_page));            
            lpn++;

        }
    }  
    else if(new_request->operation==WRITE)
    {
        while(lpn<=last_lpn)           	
        {	
            need_distb_flag=full_page;
            mask=~(0xffffffff<<(ssd->parameter->subpage_page));
            state=mask;

            if(lpn==first_lpn)
            {
                offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-new_request->lsn);
                state=state&(0xffffffff<<offset1);
            }
            if(lpn==last_lpn)
            {
                offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(new_request->lsn+new_request->size));
                state=state&(~(0xffffffff<<offset2));
            }

            ssd=insert2buffer(ssd, lpn, state,NULL,new_request);
            lpn++;
        }
    }
    complete_flag = 1;
    for(j=0;j<=(last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32;j++)
    {
        if(new_request->need_distr_flag[j] != 0)
        {
            complete_flag = 0;
        }
    }

    /*************************************************************
     *如果请求已经被全部由buffer服务，该请求可以被直接响应，输出结果
     *这里假设dram的服务时间为1000ns
     *If the request has been served entirely by the buffer, the request can be directly responded to the output.
     *This assumes that the service time of the dram is 1000ns.
     **************************************************************/
    if((complete_flag == 1)&&(new_request->subs==NULL))               
    {
        new_request->begin_time=ssd->current_time;
        new_request->response_time=ssd->current_time+1000;            
    }

    return ssd;
}

/*****************************
 *lpn向ppn的转换
 ******************************/
unsigned int lpn2ppn(struct ssd_info *ssd,unsigned int lsn)
{
    int lpn, ppn;	
    struct entry *p_map = ssd->dram->map->map_entry;
#ifdef DEBUG
    printf("enter lpn2ppn,  current time:%lld\n",ssd->current_time);
#endif
    lpn = lsn/ssd->parameter->subpage_page;			//lpn
    ppn = (p_map[lpn]).pn;
    return ppn;
}

/**********************************************************************************
 *读请求分配子请求函数，这里只处理读请求，写请求已经在buffer_management()函数中处理了
 *根据请求队列和buffer命中的检查，将每个请求分解成子请求，将子请求队列挂在channel上，
 *不同的channel有自己的子请求队列
 *The read request allocates the sub-request function. Here only the read request is processed. The write request has been processed in the buffer_management() function.
 *According to the check of the request queue and the buffer hit, each request is decomposed into sub-requests, and the sub-request queue is hung on the channel.
 *Different channels have their own subrequest queues
 **********************************************************************************/
struct ssd_info *distribute(struct ssd_info *ssd) 
{
    unsigned int start, end, first_lsn,last_lsn,lpn,flag=0,flag_attached=0,full_page;
    unsigned int j, k, sub_size;
    int i=0;
    struct request *req;
    struct sub_request *sub;
    unsigned int* complt;

#ifdef DEBUG
    printf("enter distribute,  current time:%lld\n",ssd->current_time);
#endif
    full_page=~(0xffffffff<<ssd->parameter->subpage_page);

    req = ssd->request_tail;
    if(req->response_time != 0){
        return ssd;
    }
    if (req->operation==WRITE)
    {
        return ssd;
    }

    if(req != NULL)
    {
        if(req->distri_flag == 0)
        {
            //如果还有一些读请求需要处理 | If there are still some read requests that need to be processed
            if(req->complete_lsn_count != ssd->request_tail->size)
            {
                first_lsn = req->lsn;				
                last_lsn = first_lsn + req->size;
                complt = req->need_distr_flag;
                start = first_lsn - first_lsn % ssd->parameter->subpage_page;
                end = (last_lsn/ssd->parameter->subpage_page + 1) * ssd->parameter->subpage_page;
                i = (end - start)/32;	

                while(i >= 0)
                {
                    /*************************************************************************************
                     *一个32位的整型数据的每一位代表一个子页，32/ssd->parameter->subpage_page就表示有多少页，
                     *这里的每一页的状态都存放在了 req->need_distr_flag中，也就是complt中，通过比较complt的
                     *每一项与full_page，就可以知道，这一页是否处理完成。如果没处理完成则通过creat_sub_request
                     函数创建子请求。

                     Each bit of a 32-bit integer data represents a subpage, and 32/ssd->parameter->subpage_page indicates 
                     how many pages there are. The state of each page here is stored in req->need_distr_flag, 
                     that is, In complt, by comparing each item of complt with full_page, you can know whether 
                     this page is processed or not. A subrequest is created by the creat_sub_request function 
                     if it is not processed.
                     *************************************************************************************/
                    for(j=0; j<32/ssd->parameter->subpage_page; j++)
                    {	
                        k = (complt[((end-start)/32-i)] >>(ssd->parameter->subpage_page*j)) & full_page;
                        if (k !=0)
                        {
                            lpn = start/ssd->parameter->subpage_page+ ((end-start)/32-i)*32/ssd->parameter->subpage_page + j;
                            sub_size=transfer_size(ssd,k,lpn,req);    
                            if (sub_size==0) 
                            {
                                continue;
                            }
                            else
                            {
                                sub=creat_sub_request(ssd,lpn,sub_size,0,req,req->operation);
                            }	
                        }
                    }
                    i = i-1;
                }

            }
            else
            {
                req->begin_time=ssd->current_time;
                req->response_time=ssd->current_time+1000;   
            }

        }
    }
    return ssd;
}


/**********************************************************************
 *trace_output()函数是在每一条请求的所有子请求经过process()函数处理完后，
 *打印输出相关的运行结果到outputfile文件中，这里的结果主要是运行的时间
 *====================================================================
 *The trace_output() function is executed after all sub-requests of each request have been processed by the process() function.
 *Print out the relevant running results to the outputfile, the result here is mainly the running time
 **********************************************************************/
void trace_output(struct ssd_info* ssd){
    int flag = 1; 
    int64_t start_time, end_time, latency = -1;
    struct request *req, *pre_node;
    struct sub_request *sub, *tmp;

#ifdef DEBUG
    printf("enter trace_output,  current time:%lld\n",ssd->current_time);
#endif

    pre_node=NULL;
    req = ssd->request_queue;
    start_time = 0;
    end_time = 0;

    if(req == NULL)
        return;

    while(req != NULL)	
    {
        sub = req->subs;
        flag = 1;
        start_time = 0;
        end_time = 0;
        if(req->response_time != 0)
        {
            latency = req->response_time-req->time;
            fprintf(ssd->outputfile,"%16lld %10d %6d %2d %16lld %16lld %10lld %2d %10lld\n",req->time,req->lsn, req->size, req->operation, req->begin_time, req->response_time, latency, req->meet_gc_flag, req->meet_gc_remaining_time);
            fflush(ssd->outputfile);
            fprintf(ssd->outfile_io,"%16lld %10d %6d %2d %16lld %16lld %10lld %2d %10lld\n",req->time,req->lsn, req->size, req->operation, req->begin_time, req->response_time, latency, req->meet_gc_flag, req->meet_gc_remaining_time);
            fflush(ssd->outfile_io);
            if (req->operation == WRITE) {
                fprintf(ssd->outfile_io_write,"%16lld %10d %6d %2d %16lld %16lld %10lld %2d %10lld\n",req->time,req->lsn, req->size, req->operation, req->begin_time, req->response_time, latency, req->meet_gc_flag, req->meet_gc_remaining_time);
                fflush(ssd->outfile_io_write);
            } else {
                fprintf(ssd->outfile_io_read,"%16lld %10d %6d %2d %16lld %16lld %10lld %2d %10lld\n",req->time,req->lsn, req->size, req->operation, req->begin_time, req->response_time, latency, req->meet_gc_flag, req->meet_gc_remaining_time);
                fflush(ssd->outfile_io_read);
            }

            if(req->response_time-req->begin_time==0)
            {
                printf("the response time is 0?? \n");
                getchar();
            }

            if (req->operation==READ)
            {
                ssd->read_request_count++;
                ssd->read_avg=ssd->read_avg+(req->response_time-req->time);
            } 
            else
            {
                ssd->write_request_count++;
                ssd->write_avg=ssd->write_avg+(req->response_time-req->time);
            }

            if(pre_node == NULL)
            {
                if(req->next_node == NULL)
                {
                    free(req->need_distr_flag);
                    req->need_distr_flag=NULL;
                    free(req);
                    req = NULL;
                    ssd->request_queue = NULL;
                    ssd->request_tail = NULL;
                    ssd->request_queue_length--;
                }
                else
                {
                    ssd->request_queue = req->next_node;
                    pre_node = req;
                    req = req->next_node;
                    free(pre_node->need_distr_flag);
                    pre_node->need_distr_flag=NULL;
                    free((void *)pre_node);
                    pre_node = NULL;
                    ssd->request_queue_length--;
                }
            }
            else
            {
                if(req->next_node == NULL)
                {
                    pre_node->next_node = NULL;
                    free(req->need_distr_flag);
                    req->need_distr_flag=NULL;
                    free(req);
                    req = NULL;
                    ssd->request_tail = pre_node;
                    ssd->request_queue_length--;
                }
                else
                {
                    pre_node->next_node = req->next_node;
                    free(req->need_distr_flag);
                    req->need_distr_flag=NULL;
                    free((void *)req);
                    req = pre_node->next_node;
                    ssd->request_queue_length--;
                }
            }
        }
        else
        {
            flag=1;
            while(sub != NULL)
            {
                if(start_time == 0)
                    start_time = sub->begin_time;
                if(start_time > sub->begin_time)
                    start_time = sub->begin_time;
                if(end_time < sub->complete_time)
                    end_time = sub->complete_time;
                if((sub->current_state == SR_COMPLETE)||((sub->next_state==SR_COMPLETE)&&(sub->next_state_predict_time<=ssd->current_time)))	// if any sub-request is not completed, the request is not completed
                {
                    sub = sub->next_subs;
                }
                else
                {
                    flag=0;
                    break;
                }

            }

            if (flag == 1)
            {		
                req->response_time = end_time;
                latency = end_time-req->time;
                fprintf(ssd->outputfile,"%16lld %10d %6d %2d %16lld %16lld %10lld %2d %10lld\n",req->time,req->lsn, req->size, req->operation, start_time, end_time, latency, req->meet_gc_flag, req->meet_gc_remaining_time);
                fflush(ssd->outputfile);
                fprintf(ssd->outfile_io,"%16lld %10d %6d %2d %16lld %16lld %10lld %2d %10lld\n",req->time,req->lsn, req->size, req->operation, start_time, end_time, latency, req->meet_gc_flag, req->meet_gc_remaining_time);
                fflush(ssd->outfile_io);
                if (req->operation == WRITE) {
                    fprintf(ssd->outfile_io_write,"%16lld %10d %6d %2d %16lld %16lld %10lld %2d %10lld\n",req->time,req->lsn, req->size, req->operation, start_time, end_time, latency, req->meet_gc_flag, req->meet_gc_remaining_time);
                    fflush(ssd->outfile_io_write);
                } else {
                    fprintf(ssd->outfile_io_read,"%16lld %10d %6d %2d %16lld %16lld %10lld %2d %10lld\n",req->time,req->lsn, req->size, req->operation, start_time, end_time, latency, req->meet_gc_flag, req->meet_gc_remaining_time);
                    fflush(ssd->outfile_io_read);
                }

                if(end_time-start_time==0)
                {
                    printf("the response time is 0?? \n");
                    getchar();
                }

                if (req->operation==READ)
                {
                    ssd->read_request_count++;
                    ssd->read_avg=ssd->read_avg+(end_time-req->time);
                } 
                else
                {
                    ssd->write_request_count++;
                    ssd->write_avg=ssd->write_avg+(end_time-req->time);
                }

                while(req->subs!=NULL)
                {
                    tmp = req->subs;
                    req->subs = tmp->next_subs;
                    if (tmp->update!=NULL)
                    {
                        free(tmp->update->location);
                        tmp->update->location=NULL;
                        free(tmp->update);
                        tmp->update=NULL;
                    }
                    free(tmp->location);
                    tmp->location=NULL;
                    free(tmp);
                    tmp=NULL;

                }

                if(pre_node == NULL)
                {
                    if(req->next_node == NULL)
                    {
                        free(req->need_distr_flag);
                        req->need_distr_flag=NULL;
                        free(req);
                        req = NULL;
                        ssd->request_queue = NULL;
                        ssd->request_tail = NULL;
                        ssd->request_queue_length--;
                    }
                    else
                    {
                        ssd->request_queue = req->next_node;
                        pre_node = req;
                        req = req->next_node;
                        free(pre_node->need_distr_flag);
                        pre_node->need_distr_flag=NULL;
                        free(pre_node);
                        pre_node = NULL;
                        ssd->request_queue_length--;
                    }
                }
                else
                {
                    if(req->next_node == NULL)
                    {
                        pre_node->next_node = NULL;
                        free(req->need_distr_flag);
                        req->need_distr_flag=NULL;
                        free(req);
                        req = NULL;
                        ssd->request_tail = pre_node;	
                        ssd->request_queue_length--;
                    }
                    else
                    {
                        pre_node->next_node = req->next_node;
                        free(req->need_distr_flag);
                        req->need_distr_flag=NULL;
                        free(req);
                        req = pre_node->next_node;
                        ssd->request_queue_length--;
                    }

                }
            }
            else
            {	
                pre_node = req;
                req = req->next_node;
            }
        }		
    }
}

float get_crt_free_block_prct(struct ssd_info* ssd) {
    unsigned int free_block, block_total;
    int i, j, k, l, m, n;

    block_total = free_block = 0;
    for (i = 0; i < ssd->parameter->channel_number; i++) {
        block_total += ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[i];

        for (j=0; j<ssd->parameter->chip_channel[i]; j++)
            for (k=0; k<ssd->parameter->die_chip; k++)
                for (l=0; l<ssd->parameter->plane_die; l++)
                    for (m=0; m<ssd->parameter->block_plane; m++)
                        if (ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num == ssd->parameter->page_block)
                            free_block++;
                    
    }

    return (float)free_block/(float)block_total*100.0;
}

float get_crt_free_page_prct(struct ssd_info* ssd) {
    unsigned int free_page, page_total;
    int i, j, k, l;

    page_total = free_page = 0;
    for (i = 0; i < ssd->parameter->channel_number; i++) {
        page_total += ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[i];

        for (j=0; j<ssd->parameter->chip_channel[i]; j++)
            for (k=0; k<ssd->parameter->die_chip; k++)
                for (l=0; l<ssd->parameter->plane_die; l++)
                    free_page += ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_page;            
    }

    return (float)free_page/(float)page_total*100.0;
}

float get_crt_nonempty_free_page_prct(struct ssd_info* ssd) {
    unsigned int free_page, page_total;
    int i, j, k, l, m, n;

    page_total = free_page = 0;
    for (i = 0; i < ssd->parameter->channel_number; i++) {
        page_total += ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[i];

        for (j=0; j<ssd->parameter->chip_channel[i]; j++)
            for (k=0; k<ssd->parameter->die_chip; k++)
                for (l=0; l<ssd->parameter->plane_die; l++)
                    for (m=0; m<ssd->parameter->block_plane; m++)
                        if (ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num < ssd->parameter->page_block)
                            free_page += ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num;
    }

    return (float)free_page/(float)page_total*100.0;
}

float get_crt_nonempty_free_block_prct(struct ssd_info* ssd) {
    unsigned int free_page, page_total;
    int i, j, k, l, m, n;

    page_total = free_page = 0;
    for (i = 0; i < ssd->parameter->channel_number; i++) {
        page_total += ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[i];

        for (j=0; j<ssd->parameter->chip_channel[i]; j++)
            for (k=0; k<ssd->parameter->die_chip; k++)
                for (l=0; l<ssd->parameter->plane_die; l++)
                    for (m=0; m<ssd->parameter->block_plane; m++)
                        if (ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num < ssd->parameter->page_block)
                            free_page++;
    }

    return (float)free_page/(float)page_total*100.0;
}

/*******************************************************************************
 *statistic_output()函数主要是输出处理完一条请求后的相关处理信息。
 *1，计算出每个plane的擦除次数即plane_erase和总的擦除次数即erase
 *2，打印min_lsn，max_lsn，read_count，program_count等统计信息到文件outputfile中。
 *3，打印相同的信息到文件statisticfile中
 *******************************************************************************/
void statistic_output(struct ssd_info *ssd)
{
    unsigned int lpn_count=0,i,j,k,m,erase=0,plane_erase=0;
    double gc_energy=0.0;
#ifdef DEBUG
    printf("enter statistic_output,  current time:%lld\n",ssd->current_time);
#endif

    for(i=0;i<ssd->parameter->channel_number;i++)
    {
        for(j=0;j<ssd->parameter->die_chip;j++)
        {
            for(k=0;k<ssd->parameter->plane_die;k++)
            {
                plane_erase=0;
                for(m=0;m<ssd->parameter->block_plane;m++)
                {
                    if(ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].blk_head[m].erase_count>0)
                    {
                        erase=erase+ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].blk_head[m].erase_count;
                        plane_erase+=ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].blk_head[m].erase_count;
                    }
                }
                fprintf(ssd->outputfile,"the %d channel, %d chip, %d die, %d plane has : %13d erase operations\n",i,j,k,m,plane_erase);
                fprintf(ssd->statisticfile,"the %d channel, %d chip, %d die, %d plane has : %13d erase operations\n",i,j,k,m,plane_erase);
            }
        }
    }

    fprintf(ssd->outputfile,"\n");
    fprintf(ssd->outputfile,"\n");
    fprintf(ssd->outputfile,"---------------------------statistic data---------------------------\n");	 
    fprintf(ssd->outputfile,"min lsn: %13d\n",ssd->min_lsn);	
    fprintf(ssd->outputfile,"max lsn: %13d\n",ssd->max_lsn);
    fprintf(ssd->outputfile,"read count: %13lu\n",ssd->read_count);	  
    fprintf(ssd->outputfile,"program count: %13lu",ssd->program_count);	
    fprintf(ssd->outputfile,"                        include the flash write count leaded by read requests\n");
    fprintf(ssd->outputfile,"the read operation leaded by un-covered update count: %13d\n",ssd->update_read_count);
    fprintf(ssd->outputfile,"erase count: %13lu\n",ssd->erase_count);
    fprintf(ssd->outputfile,"direct erase count: %13lu\n",ssd->direct_erase_count);
    fprintf(ssd->outputfile,"copy back count: %13lu\n",ssd->copy_back_count);
    fprintf(ssd->outputfile,"multi-plane program count: %13lu\n",ssd->m_plane_prog_count);
    fprintf(ssd->outputfile,"multi-plane read count: %13lu\n",ssd->m_plane_read_count);
    fprintf(ssd->outputfile,"interleave write count: %13lu\n",ssd->interleave_count);
    fprintf(ssd->outputfile,"interleave read count: %13lu\n",ssd->interleave_read_count);
    fprintf(ssd->outputfile,"interleave two plane and one program count: %13lu\n",ssd->inter_mplane_prog_count);
    fprintf(ssd->outputfile,"interleave two plane count: %13lu\n",ssd->inter_mplane_count);
    fprintf(ssd->outputfile,"gc copy back count: %13lu\n",ssd->gc_copy_back);
    fprintf(ssd->outputfile,"write flash count: %13lu\n",ssd->write_flash_count);
    fprintf(ssd->outputfile,"interleave erase count: %13lu\n",ssd->interleave_erase_count);
    fprintf(ssd->outputfile,"multiple plane erase count: %13lu\n",ssd->mplane_erase_conut);
    fprintf(ssd->outputfile,"interleave multiple plane erase count: %13lu\n",ssd->interleave_mplane_erase_count);
    fprintf(ssd->outputfile,"read request count: %13u\n",ssd->read_request_count);
    fprintf(ssd->outputfile,"write request count: %13u\n",ssd->write_request_count);
    fprintf(ssd->outputfile,"read request average size: %13f\n",ssd->ave_read_size);
    fprintf(ssd->outputfile,"write request average size: %13f\n",ssd->ave_write_size);
    if (ssd->read_request_count != 0)
        fprintf(ssd->outputfile,"read request average response time: %lld\n",ssd->read_avg/ssd->read_request_count);
    if (ssd->write_request_count != 0)
        fprintf(ssd->outputfile,"write request average response time: %lld\n",ssd->write_avg/ssd->write_request_count);
    fprintf(ssd->outputfile,"buffer read hits: %13lu\n",ssd->dram->buffer->read_hit);
    fprintf(ssd->outputfile,"buffer read miss: %13lu\n",ssd->dram->buffer->read_miss_hit);
    fprintf(ssd->outputfile,"buffer write hits: %13lu\n",ssd->dram->buffer->write_hit);
    fprintf(ssd->outputfile,"buffer write miss: %13lu\n",ssd->dram->buffer->write_miss_hit);
    fprintf(ssd->outputfile,"erase: %13u\n",erase);
    fprintf(ssd->outputfile,"write amplification: %.2f\n",(double)ssd->program_count/(double)ssd->write_request_count);
    fprintf(ssd->outputfile,"read amplification: %.2f\n",(double)ssd->read_count/(double)ssd->read_request_count);
    fflush(ssd->outputfile);


    fprintf(ssd->statisticfile,"\n");
    fprintf(ssd->statisticfile,"\n");
    fprintf(ssd->statisticfile,"---------------------------statistic data---------------------------\n");	
    fprintf(ssd->statisticfile,"min lsn: %13u\n",ssd->min_lsn);	
    fprintf(ssd->statisticfile,"max lsn: %13u\n",ssd->max_lsn);
    fprintf(ssd->statisticfile,"read count: %13lu\n",ssd->read_count);	  
    fprintf(ssd->statisticfile,"program count: %13lu",ssd->program_count);	  
    fprintf(ssd->statisticfile,"                        include the flash write count leaded by read requests\n");
    fprintf(ssd->statisticfile,"the read operation leaded by un-covered update count: %13u\n",ssd->update_read_count);
    fprintf(ssd->statisticfile,"erase count: %13lu\n",ssd->erase_count);	  
    fprintf(ssd->statisticfile,"direct erase count: %13lu\n",ssd->direct_erase_count);
    fprintf(ssd->statisticfile,"copy back count: %13lu\n",ssd->copy_back_count);
    fprintf(ssd->statisticfile,"multi-plane program count: %13lu\n",ssd->m_plane_prog_count);
    fprintf(ssd->statisticfile,"multi-plane read count: %13lu\n",ssd->m_plane_read_count);
    fprintf(ssd->statisticfile,"interleave count: %13lu\n",ssd->interleave_count);
    fprintf(ssd->statisticfile,"interleave read count: %13lu\n",ssd->interleave_read_count);
    fprintf(ssd->statisticfile,"interleave two plane and one program count: %13lu\n",ssd->inter_mplane_prog_count);
    fprintf(ssd->statisticfile,"interleave two plane count: %13lu\n",ssd->inter_mplane_count);
    fprintf(ssd->statisticfile,"gc copy back count: %13lu\n",ssd->gc_copy_back);
    fprintf(ssd->statisticfile,"gc count: %13lu\n",ssd->num_gc);
    fprintf(ssd->statisticfile,"write flash count: %13lu\n",ssd->write_flash_count);
    fprintf(ssd->statisticfile,"waste page count: %13lu\n",ssd->waste_page_count);
    fprintf(ssd->statisticfile,"interleave erase count: %13lu\n",ssd->interleave_erase_count);
    fprintf(ssd->statisticfile,"multiple plane erase count: %13lu\n",ssd->mplane_erase_conut);
    fprintf(ssd->statisticfile,"interleave multiple plane erase count: %13lu\n",ssd->interleave_mplane_erase_count);
    fprintf(ssd->statisticfile,"read request count: %13u\n",ssd->read_request_count);
    fprintf(ssd->statisticfile,"write request count: %13u\n",ssd->write_request_count);
    fprintf(ssd->statisticfile,"read request average size: %13f\n",ssd->ave_read_size);
    fprintf(ssd->statisticfile,"write request average size: %13f\n",ssd->ave_write_size);
    if(ssd->read_request_count != 0)
        fprintf(ssd->statisticfile,"read request average response time: %lld\n",ssd->read_avg/ssd->read_request_count);
    if(ssd->write_request_count != 0)
        fprintf(ssd->statisticfile,"write request average response time: %lld\n",ssd->write_avg/ssd->write_request_count);
    fprintf(ssd->statisticfile,"buffer read hits: %13lu\n",ssd->dram->buffer->read_hit);
    fprintf(ssd->statisticfile,"buffer read miss: %13lu\n",ssd->dram->buffer->read_miss_hit);
    fprintf(ssd->statisticfile,"buffer write hits: %13lu\n",ssd->dram->buffer->write_hit);
    fprintf(ssd->statisticfile,"buffer write miss: %13lu\n",ssd->dram->buffer->write_miss_hit);
    fprintf(ssd->statisticfile,"erase: %13u\n",erase);
    fprintf(ssd->statisticfile,"write sub request count: %13u\n",ssd->write_subreq_count);
    fprintf(ssd->statisticfile,"read subr request count: %13u\n",ssd->read_subreq_count);
    if(ssd->write_request_count != 0)
        fprintf(ssd->statisticfile,"write amplification: %.2f\n",(double)ssd->program_count/(double)ssd->write_subreq_count);
    if(ssd->read_request_count != 0)
        fprintf(ssd->statisticfile,"read amplification: %.2f\n",(double)ssd->read_count/(double)ssd->read_subreq_count);
    fprintf(ssd->statisticfile, "write amplification (size): %.2f\n", (double)ssd->in_program_size/(double)ssd->write_request_size);
    fprintf(ssd->statisticfile, "read amplification (size): %.2f\n", (double)ssd->in_read_size/(double)ssd->read_request_size);
    fprintf(ssd->statisticfile, "avg. gc page move: %.2f (%.2f%%)\n", (double)ssd->gc_move_page/(double)ssd->num_gc, (100*((double)ssd->gc_move_page/(double)ssd->num_gc)/ssd->parameter->page_block));
    fprintf(ssd->statisticfile, "gc time window: %lld\n", ssd->gc_time_window);
    fprintf(ssd->statisticfile, "\n\n simulation duration: %lld ns\n", ssd->simulation_end_time - ssd->simulation_start_time);
    fprintf(ssd->statisticfile, " IOPS: %.3f\n", (double)(ssd->read_count+ssd->program_count)/((double)(ssd->simulation_end_time - ssd->simulation_start_time)/1000000000));
    fprintf(ssd->statisticfile, " read BW: %.3f MB/s\n", ((double)ssd->read_request_size/2000.0)/ ((double)(ssd->simulation_end_time - ssd->simulation_start_time)/1000000000));
    fprintf(ssd->statisticfile, " write BW: %.3f MB/s\n", ((double)ssd->write_request_size/2000.0)/ ((double)(ssd->simulation_end_time - ssd->simulation_start_time)/1000000000));
    fflush(ssd->statisticfile);

    printf(" simulation duration: %lld ns\n", ssd->simulation_end_time - ssd->simulation_start_time);
    printf(" IOPS: %.3f\n", (double)(ssd->read_count+ssd->program_count)/((double)(ssd->simulation_end_time - ssd->simulation_start_time)/1000000000));
    printf(" read BW: %.3f MB/s\n", ((double)ssd->read_request_size/2000.0)/ ((double)(ssd->simulation_end_time - ssd->simulation_start_time)/1000000000));
    printf(" write BW: %.3f MB/s\n", ((double)ssd->write_request_size/2000.0)/ ((double)(ssd->simulation_end_time - ssd->simulation_start_time)/1000000000));
}


/***********************************************************************************
 *根据每一页的状态计算出每一需要处理的子页的数目，也就是一个子请求需要处理的子页的页数
 *Calculate the number of subpages that need to be processed according to the state of each page, that is, the number of pages of subpages that a subrequest needs to process.
 ************************************************************************************/
unsigned int size(unsigned int stored)
{
    unsigned int i,total=0,mask=0x80000000;

#ifdef DEBUG
    printf("enter size\n");
#endif
    for(i=1;i<=32;i++)
    {
        if(stored & mask) total++;
        stored<<=1;
    }
#ifdef DEBUG
    printf("leave size\n");
#endif
    return total;
}


/*********************************************************
 *transfer_size()函数的作用就是计算出子请求的需要处理的size
 *函数中单独处理了first_lpn，last_lpn这两个特别情况，因为这
 *两种情况下很有可能不是处理一整页而是处理一页的一部分，因
 *为lsn有可能不是一页的第一个子页。
 *********************************************************/
unsigned int transfer_size(struct ssd_info *ssd,int need_distribute,unsigned int lpn,struct request *req)
{
    unsigned int first_lpn,last_lpn,state,trans_size;
    unsigned int mask=0,offset1=0,offset2=0;

    first_lpn=req->lsn/ssd->parameter->subpage_page;
    last_lpn=(req->lsn+req->size-1)/ssd->parameter->subpage_page;

    mask=~(0xffffffff<<(ssd->parameter->subpage_page));
    state=mask;
    if(lpn==first_lpn)
    {
        offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-req->lsn);
        state=state&(0xffffffff<<offset1);
    }
    if(lpn==last_lpn)
    {
        offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(req->lsn+req->size));
        state=state&(~(0xffffffff<<offset2));
    }

    trans_size=size(state&need_distribute);

    return trans_size;
}


/**********************************************************************************************************  
 *int64_t find_nearest_event(struct ssd_info *ssd)       
 *寻找所有子请求的最早到达的下个状态时间,首先看请求的下一个状态时间，如果请求的下个状态时间小于等于当前时间，
 *说明请求被阻塞，需要查看channel或者对应die的下一状态时间。Int64是有符号 64 位整数数据类型，值类型表示值介于
 *-2^63 ( -9,223,372,036,854,775,808)到2^63-1(+9,223,372,036,854,775,807 )之间的整数。存储空间占 8 字节。
 *channel,die是事件向前推进的关键因素，三种情况可以使事件继续向前推进，channel，die分别回到idle状态，die中的
 *读数据准备好了
 *Find the next state time of the earliest arrival of all sub-requests, first look at the next state time of the request, if the next state time of the request is less than or equal to the current time,
 *Indicates that the request is blocked. You need to check the channel or the next status time of the corresponding die. Int64 is a signed 64-bit integer data type, and the value type indicates that the value is between
 *An integer between -2^63 (-9,223,372,036,854,775,808) to 2^63-1 (+9,223,372,036,854,775,807). The storage space is 8 bytes.
 *Channel, die is the key factor for the event to advance. Three situations can make the event continue to move forward. The channel and die return to the idle state respectively.
 *Read the data is ready
 ***********************************************************************************************************/
int64_t find_nearest_event(struct ssd_info *ssd) 
{
    unsigned int i,j;
    int64_t time=MAX_INT64;
    int64_t time1=MAX_INT64;
    int64_t time2=MAX_INT64;

    for (i=0;i<ssd->parameter->channel_number;i++)
    {
        if (ssd->channel_head[i].next_state==CHANNEL_IDLE)
            if(time1>ssd->channel_head[i].next_state_predict_time)
                if (ssd->channel_head[i].next_state_predict_time>ssd->current_time)    
                    time1=ssd->channel_head[i].next_state_predict_time;
        for (j=0;j<ssd->parameter->chip_channel[i];j++)
        {
            if ((ssd->channel_head[i].chip_head[j].next_state==CHIP_IDLE)||(ssd->channel_head[i].chip_head[j].next_state==CHIP_DATA_TRANSFER))
                if(time2>ssd->channel_head[i].chip_head[j].next_state_predict_time)
                    if (ssd->channel_head[i].chip_head[j].next_state_predict_time>ssd->current_time)    
                        time2=ssd->channel_head[i].chip_head[j].next_state_predict_time;	
        }   
    } 

    /*****************************************************************************************************
     *time为所有 A.下一状态为CHANNEL_IDLE且下一状态预计时间大于ssd当前时间的CHANNEL的下一状态预计时间
     *           B.下一状态为CHIP_IDLE且下一状态预计时间大于ssd当前时间的DIE的下一状态预计时间
     *		     C.下一状态为CHIP_DATA_TRANSFER且下一状态预计时间大于ssd当前时间的DIE的下一状态预计时间
     *CHIP_DATA_TRANSFER读准备好状态，数据已从介质传到了register，下一状态是从register传往buffer中的最小值 
     *注意可能都没有满足要求的time，这时time返回0x7fffffffffffffff 。
     *****************************************************************************************************/
    time=(time1>time2)?time2:time1;
    return time;
}

/***********************************************
 *free_all_node()函数的作用就是释放所有申请的节点
 ************************************************/
void free_all_node(struct ssd_info *ssd)
{
    unsigned int i,j,k,l,n;
    struct buffer_group *pt=NULL;
    struct direct_erase * erase_node=NULL;
    for (i=0;i<ssd->parameter->channel_number;i++)
    {
        for (j=0;j<ssd->parameter->chip_channel[0];j++)
        {
            for (k=0;k<ssd->parameter->die_chip;k++)
            {
                for (l=0;l<ssd->parameter->plane_die;l++)
                {
                    for (n=0;n<ssd->parameter->block_plane;n++)
                    {
                        free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head);
                        ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head=NULL;
                    }
                    free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head);
                    ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head=NULL;
                    while(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node!=NULL)
                    {
                        erase_node=ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node;
                        ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node=erase_node->next_node;
                        free(erase_node);
                        erase_node=NULL;
                    }
                }

                free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head);
                ssd->channel_head[i].chip_head[j].die_head[k].plane_head=NULL;
            }
            free(ssd->channel_head[i].chip_head[j].die_head);
            ssd->channel_head[i].chip_head[j].die_head=NULL;
        }
        free(ssd->channel_head[i].chip_head);
        ssd->channel_head[i].chip_head=NULL;
    }
    free(ssd->channel_head);
    ssd->channel_head=NULL;

    avlTreeDestroy( ssd->dram->buffer);
    ssd->dram->buffer=NULL;

    free(ssd->dram->map->map_entry);
    ssd->dram->map->map_entry=NULL;
    free(ssd->dram->map);
    ssd->dram->map=NULL;
    free(ssd->dram);
    ssd->dram=NULL;
    free(ssd->parameter);
    ssd->parameter=NULL;

    free(ssd);
    ssd=NULL;
}


/*****************************************************************************
 *make_aged()函数的作用就死模拟真实的用过一段时间的ssd，
 *那么这个ssd的相应的参数就要改变，所以这个函数实质上就是对ssd中各个参数的赋值。
 ******************************************************************************/
struct ssd_info *make_aged(struct ssd_info *ssd)
{
    unsigned int i,j,k,l,m,n,ppn;
    int threshould,flag=0;

    if (ssd->parameter->aged==1)
    {
        //threshold表示一个plane中有多少页需要提前置为失效
        threshould=(int)(ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->aged_ratio);  
        for (i=0;i<ssd->parameter->channel_number;i++)
            for (j=0;j<ssd->parameter->chip_channel[i];j++)
                for (k=0;k<ssd->parameter->die_chip;k++)
                    for (l=0;l<ssd->parameter->plane_die;l++)
                    {  
                        flag=0;
                        for (m=0;m<ssd->parameter->block_plane;m++)
                        {  
                            if (flag>=threshould)
                            {
                                break;
                            }
                            for (n=0;n<(ssd->parameter->page_block*ssd->parameter->aged_ratio+1);n++)
                            {  
                                ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].valid_state=0;        //表示某一页失效，同时标记valid和free状态都为0
                                ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].free_state=0;         //表示某一页失效，同时标记valid和free状态都为0
                                ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].lpn=0;  //把valid_state free_state lpn都置为0表示页失效，检测的时候三项都检测，单独lpn=0可以是有效页
                                ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num--;
                                ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].invalid_page_num++;
                                ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].last_write_page++;
                                ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_page--;
                                flag++;

                                ppn=find_ppn(ssd,i,j,k,l,m,n);

                            }
                        } 
                    }	 
    }  
    else
    {
        return ssd;
    }

    return ssd;
}

struct ssd_info *warmup(struct ssd_info *ssd) {
    int channel, chip, die, plane, block, i, valid_state;
    float threshold = ssd->parameter->aged_ratio;
    int64_t lpn=0, crt_lpn=0, pg_count=0;
    int64_t pg_threshold=ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num * (1-ssd->parameter->overprovide);

    for (channel=0; channel<ssd->parameter->channel_number; channel++)
        for (chip=0; chip<ssd->channel_head[channel].chip; chip++)
            for (die=0; die<ssd->parameter->die_chip; die++)
                for (plane=0; plane<ssd->parameter->plane_die; plane++)
                    for (block=0; block<ssd->parameter->block_plane; block++)
                        for (i=0; i<ssd->parameter->page_block && pg_count < pg_threshold; i++) {
                            pg_count++;

                            // fill with valid page
                            if (i < ssd->parameter->page_block*threshold) {
                                valid_state = 0xffffffff;
                                lpn = crt_lpn++;
                                ssd->dram->map->map_entry[lpn].pn=find_ppn(ssd, channel, chip, die, plane, block, i);
                                ssd->dram->map->map_entry[lpn].state=valid_state;

                            } else { // fill with invalid page
                                valid_state = 0x0;
                                lpn = 0;
                                ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num++;
                            }

                            ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state=valid_state;
                            ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state=0x0;
                            ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn=lpn;
                            ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;
                            ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page++;
                            ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;

                        }
    return ssd;
}

/*********************************************************************************************
 *no_buffer_distribute()函数是处理当ssd没有dram的时候，
 *这是读写请求就不必再需要在buffer里面寻找，直接利用creat_sub_request()函数创建子请求，再处理。
 *The no_buffer_distribute() function is used when ssd has no dram. This is a read/write request. 
 *You don't need to look in the buffer. You can use the creat_sub_request() function to create 
 *a subrequest and then process it.
 *********************************************************************************************/
struct ssd_info *no_buffer_distribute(struct ssd_info *ssd)
{
    unsigned int lsn,lpn,last_lpn,first_lpn,complete_flag=0, state;
    unsigned int flag=0,flag1=1,active_region_flag=0;           //to indicate the lsn is hitted or not
    struct request *req=NULL;
    struct sub_request *sub=NULL,*sub_r=NULL,*update=NULL;
    struct local *loc=NULL;
    struct channel_info *p_ch=NULL;


    unsigned int mask=0; 
    unsigned int offset1=0, offset2=0;
    unsigned int sub_size=0;
    unsigned int sub_state=0;

    ssd->dram->current_time=ssd->current_time;
    req=ssd->request_tail;       
    lsn=req->lsn;
    lpn=req->lsn/ssd->parameter->subpage_page;
    last_lpn=(req->lsn+req->size-1)/ssd->parameter->subpage_page;
    first_lpn=req->lsn/ssd->parameter->subpage_page;

    if(req->operation==READ)        
    {		
        while(lpn<=last_lpn) 		
        {
            sub_state=(ssd->dram->map->map_entry[lpn].state&0x7fffffff);
            sub_size=size(sub_state);
            sub=creat_sub_request(ssd,lpn,sub_size,sub_state,req,req->operation);
            lpn++;
        }
    }
    else if(req->operation==WRITE)
    {
        while(lpn<=last_lpn)     	
        {	
            mask=~(0xffffffff<<(ssd->parameter->subpage_page));
            state=mask;
            if(lpn==first_lpn)
            {
                offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-req->lsn);
                state=state&(0xffffffff<<offset1);
            }
            if(lpn==last_lpn)
            {
                offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(req->lsn+req->size));
                state=state&(~(0xffffffff<<offset2));
            }
            sub_size=size(state);

            sub=creat_sub_request(ssd,lpn,sub_size,state,req,req->operation);
            lpn++;
        }
    }

    return ssd;
}

void display_title() 
{
    printf("\n");
    printf("               _     _             \n");
    printf("              | |   (_)            \n");
    printf("   ___ ___  __| |___ _ _ __ ___    \n");
    printf("  / __/ __|/ _` / __| | '_ ` _ \\   \n");
    printf("  \\__ \\__ \\ (_| \\__ \\ | | | | | |  \n");
    printf("  |___/___/\\__,_|___/_|_| |_| |_|  \n");
    printf("                                   \n");
    printf("  SSD internal simulation tool,\n  created by Yang Hu, v.2.0. Modified by Fadhil Kurnia \n\n");
}

void display_help() 
{
    printf("  usage: ssd [options] trace_file\n");
    printf("    options:\n");
    printf("     --timestamp <time> \t 15 chars timestamp used for log directory name (e.g: 20190214_220000), the default is current time\n");
    printf("     --parameter <filename> \t parameter filename (default: page.parameter)\n");
    printf("     --raid0 \t\t\t run raid 0 simulation\n");
    printf("     --raid5 \t\t\t run raid 5 simulation\n");
    printf("     --ndisk <num_disk> \t number of disk for raid simulation\n\n");
}

void display_simulation_intro(struct ssd_info *ssd)
{
    printf("\n\nBegin simulating ... ... ... ...\n");
    printf("  -parameter file: %s\n",ssd->parameterfilename); 
    printf("  -trace file    : %s\n",ssd->tracefilename);
    printf("\n\n   ^o^    OK, please wait a moment, and enjoy music and coffee   ^o^    \n\n");
}

void display_freepage(struct ssd_info *ssd)
{
    int i, j, k;
    for (i=0;i<ssd->parameter->channel_number;i++)
        for (j=0;j<ssd->parameter->die_chip;j++)
            for (k=0;k<ssd->parameter->plane_die;k++)
                printf("%d,0,%d,%d:  %5d\n",i,j,k,ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].free_page);
}

void prep_output_for_simulation(struct ssd_info *ssd) 
{
    fprintf(ssd->outputfile,"      arrive           lsn     size ope     begin time    response time    process time\n");	
    fflush(ssd->outputfile);
}

void close_file(struct ssd_info *ssd)
{
    if (ssd->tracefile!=NULL) fclose(ssd->tracefile);
    if (ssd->outputfile) fclose(ssd->outputfile);
    if (ssd->statisticfile) fclose(ssd->statisticfile);
    if (ssd->statisticfile2) fclose(ssd->statisticfile2);
    if (ssd->outfile_gc) fclose(ssd->outfile_gc);
}

// Get current time in string for log directory name
void get_current_time(char *current_time) {
    time_t timer;
    struct tm* tm_info;

    time(&timer);
    tm_info = localtime(&timer);

    strftime(current_time, 26, "%Y%m%d_%H%M%S", tm_info);
}

void display_state(struct ssd_info *ssd) {
    int channel, chip, die, plane, block;
    unsigned int page_block = ssd->parameter->page_block;
    unsigned int valid_pg, invalid_pg, free_pg;
    int64_t total_page=0, total_valid_pg=0, total_invalid_pg=0, total_free_pg=0;
    int64_t total_bk=0, total_full_vbk=0, total_empty_bk=0;
    FILE *fp = NULL;

    // Preparing to write to external file
    fp = fopen("state.dat","w");

    printf("State of the SSD:\n");
    for (channel=0; channel<ssd->parameter->channel_number; channel++)
        for (chip=0; chip<ssd->channel_head[channel].chip; chip++) 
            for (die=0; die<ssd->channel_head[channel].chip_head[chip].die_num; die++)
                for (plane=0; plane<ssd->parameter->plane_die; plane++)
                    for (block=0; block<ssd->parameter->block_plane; block++) {
                        free_pg = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num;
                        invalid_pg = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num;
                        valid_pg = page_block-free_pg-invalid_pg;
                        printf("cnl:%d chip:%d die:%d pln:%d blk:%d  frpg:%u ivpg:%u vpg:%u\n", channel, chip, die, plane, block, free_pg, invalid_pg, valid_pg);
                        fprintf(fp, "%d %d %d %d %d | %u %u %u\n", channel, chip, die, plane, block, free_pg, invalid_pg, valid_pg);

                        total_page += ssd->parameter->page_block;
                        total_valid_pg += valid_pg;
                        total_invalid_pg += invalid_pg;
                        total_free_pg += free_pg;
                        total_bk++;
                        if (valid_pg==ssd->parameter->page_block) total_full_vbk++;
                        if (free_pg==ssd->parameter->page_block) total_empty_bk++;
                    }

    printf("State of block and page in the SSD:\n");
    printf("  total_block:%lld free_block:%lld(%.4f) full_block:%lld(%.4f)\n", total_bk, total_empty_bk, (float)total_empty_bk/(float)total_bk, total_full_vbk, (float)total_full_vbk/(float)total_bk);
    printf("  total_page:%lld avg_free_page/block:%.4f avg_valid_page/block:%.4f avg_invalid_page/block:%.4f\n", total_page, ((float)total_free_pg/(float)total_bk), ((float)total_valid_pg/(float)total_bk), ((float)total_invalid_pg/(float)total_bk));
    fclose(fp);
}

struct ssd_info *init_gc(struct ssd_info *ssd) {
    struct gc_operation* gc_node = NULL;
    unsigned int channel=0, chip=0, die=0, plane=0, is_gc_inited, free_page;
    unsigned int threshold;
    int64_t gc_start_time = 0;

    // Don't check when #free-page > threshold
    threshold = ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num * (1-ssd->parameter->overprovide) * ssd->parameter->gc_hard_threshold;
    free_page = 0;
    for(channel=0; channel<ssd->parameter->channel_number; channel++) {
        for(chip=0; chip<ssd->channel_head[channel].chip; chip++) {
            for(die=0; die<ssd->parameter->die_chip; die++) {
                for(plane=0; plane<ssd->parameter->die_chip; plane++) {
                    free_page += ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page;
                }
            }
        }
    }
    if (free_page > threshold) {
        return ssd;
    }
    
    // Check whether GC need to be run in each plane
    for(channel=0; channel<ssd->parameter->channel_number; channel++) {
        for(chip=0; chip<ssd->channel_head[channel].chip; chip++) {
            for(die=0; die<ssd->parameter->die_chip; die++) {
                for(plane=0; plane<ssd->parameter->die_chip; plane++) {
                    if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold)) {
                        // check whether gc process already initialized for this plane
                        is_gc_inited=1;
                        gc_node=ssd->channel_head[channel].gc_command;
                        while(gc_node!=NULL) {
                            if (gc_node->chip==chip && gc_node->die==die && gc_node->plane==plane) {
                                is_gc_inited = 0;
                                break;
                            }
                            gc_node=gc_node->next_node;
                        }
                        if(is_gc_inited) {
                            gc_node=(struct gc_operation *)malloc(sizeof(struct gc_operation));
                            alloc_assert(gc_node,"gc_node");
                            memset(gc_node,0, sizeof(struct gc_operation));

                            gc_node->next_node=NULL;
                            gc_node->chip=chip;
                            gc_node->die=die;
                            gc_node->plane=plane;
                            gc_node->block=0xffffffff;
                            gc_node->page=0;
                            gc_node->state=GC_WAIT;
                            gc_node->priority=GC_UNINTERRUPT;
                            gc_node->next_node=ssd->channel_head[channel].gc_command;
                            gc_node->x_init_time = ssd->channel_head[channel].current_time;
                            gc_node->x_free_percentage = (double) ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page / (double) (ssd->parameter->page_block*ssd->parameter->block_plane) * (double) 100;
                            gc_node->x_moved_pages=0;

                            ssd->channel_head[channel].gc_command=gc_node;
                            ssd->gc_request++;
                        }
                    }
                }
            }
        }
    }

    return ssd;
}

void print_gc_node(struct ssd_info* ssd) {
    struct gc_operation* gc = NULL;
    int channel = 0;
    for(channel=0; channel<ssd->parameter->channel_number; channel++) {
        gc = ssd->channel_head[channel].gc_command;
        while (gc!=NULL) {
            printf("/////////// %d %d %d %d %lld %lld %d %lld %d %lld\n", channel, gc->chip, gc->die, gc->plane, gc->x_init_time, gc->x_expected_start_time, ssd->channel_head[channel].current_state, ssd->channel_head[channel].current_time, ssd->channel_head[channel].next_state, ssd->channel_head[channel].next_state_predict_time);
            gc=gc->next_node;
        }
    }
    
}