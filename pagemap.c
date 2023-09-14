/*****************************************************************************************************************************
  This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
  Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

  FileName： pagemap.h
Author: Hu Yang		Version: 2.1	Date:2011/12/02
Description: 

History:
<contributor>     <time>        <version>       <desc>                   <e-mail>
Yang Hu	        2009/09/25	      1.0		    Creat SSDsim       yanghu@foxmail.com
2010/05/01        2.x           Change 
Zhiming Zhu     2011/07/01        2.0           Change               812839842@qq.com
Shuangwu Zhang  2011/11/01        2.1           Change               820876427@qq.com
Chao Ren        2011/07/01        2.0           Change               529517386@qq.com
Hao Luo         2011/01/01        2.0           Change               luohao135680@gmail.com
 *****************************************************************************************************************************/

#define _CRTDBG_MAP_ALLOC
#include<stdio.h>
#include "pagemap.h"
#include "flash.h"
#include "ssd.h"
#include "raid.h"


FILE *fp;
char filename[] = "Block_count.txt";

/************************************************
 *断言,当打开文件失败时，输出“open 文件名 error”
 *************************************************/
void file_assert(int error,char *s)
{
    if(error == 0) return;
    printf("open %s error\n",s);
    getchar();
    exit(-1);
}

/*****************************************************
 *断言,当申请内存空间失败时，输出“malloc 变量名 error”
 ******************************************************/
void alloc_assert(void *p,char *s)//断言
{
    if(p!=NULL) return;
    printf("malloc %s error\n",s);
    getchar();
    exit(-1);
}

/*********************************************************************************
 *断言
 *A，读到的time_t，device，lsn，size，ope都<0时，输出“trace error:.....”
 *B，读到的time_t，device，lsn，size，ope都=0时，输出“probable read a blank line”
 **********************************************************************************/
void trace_assert(int64_t time_t,int device,unsigned int lsn,int size,int ope)//断言
{
    if(time_t <0 || device < 0 || lsn < 0 || size < 0 || ope < 0)
    {
        printf("trace error:%lld %d %d %d %d\n",time_t,device,lsn,size,ope);
        getchar();
        exit(-1);
    }
    if(time_t == 0 && device == 0 && lsn == 0 && size == 0 && ope == 0)
    {
        printf("probable read a blank line\n");
        getchar();
    }
}


/************************************************************************************
*The function of the function is to find the channel, chip, die, plane, block, page where the physical page is located according to the physical page number ppn
*The obtained channel, chip, die, plane, block, and page are placed in the structure location and used as the return value
 *************************************************************************************/
struct local *find_location(struct ssd_info *ssd, unsigned int ppn)
{
    struct local *location=NULL;
    unsigned int i=0;
    int pn,ppn_value=ppn;
    int page_plane=0,page_die=0,page_chip=0,page_channel=0;

    pn = ppn;

#ifdef DEBUG
    printf("enter find_location\n");
#endif

    location=(struct local *)malloc(sizeof(struct local));
    alloc_assert(location,"location");
    memset(location,0, sizeof(struct local));

    page_plane=ssd->parameter->page_block*ssd->parameter->block_plane;
    page_die=page_plane*ssd->parameter->plane_die;
    page_chip=page_die*ssd->parameter->die_chip;
    page_channel=page_chip*ssd->parameter->chip_channel[0];

    /*******************************************************************************
     *page_channel is the number of pages in a channel, ppn/page_channel will get which channel it is in
     *You can get chip, die, plane, block, page in the same way
     ********************************************************************************/
    location->channel = ppn/page_channel;
    location->chip = (ppn%page_channel)/page_chip;
    location->die = ((ppn%page_channel)%page_chip)/page_die;
    location->plane = (((ppn%page_channel)%page_chip)%page_die)/page_plane;
    location->block = ((((ppn%page_channel)%page_chip)%page_die)%page_plane)/ssd->parameter->page_block;
    location->page = (((((ppn%page_channel)%page_chip)%page_die)%page_plane)%ssd->parameter->page_block)%ssd->parameter->page_block;

    return location;
}


/*****************************************************************************
 *这个函数的功能是根据参数channel，chip，die，plane，block，page，找到该物理页号
 *函数的返回值就是这个物理页号
 * The function of this function is to find the physical page number according to the parameters channel, chip, die, plane, block, page.
 * The return value of the function is the physical page number
 ******************************************************************************/
unsigned int find_ppn(struct ssd_info * ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int block,unsigned int page)
{
    unsigned int ppn=0;
    unsigned int i=0;
    int page_plane=0,page_die=0,page_chip=0;
    int page_channel[100];                  /*这个数组存放的是每个channel的page数目 | This array stores the number of pages per channel*/

#ifdef DEBUG
    printf("enter find_psn,channel:%d, chip:%d, die:%d, plane:%d, block:%d, page:%d\n",channel,chip,die,plane,block,page);
#endif

    /*********************************************
     *计算出plane，die，chip，channel中的page的数目 | Calculate the number of pages in plane, die, chip, channel
     **********************************************/
    page_plane=ssd->parameter->page_block*ssd->parameter->block_plane;
    page_die=page_plane*ssd->parameter->plane_die;
    page_chip=page_die*ssd->parameter->die_chip;
    while(i<ssd->parameter->channel_number)
    {
        page_channel[i]=ssd->parameter->chip_channel[i]*page_chip;
        i++;
    }

    /****************************************************************************
     *计算物理页号ppn，ppn是channel，chip，die，plane，block，page中page个数的总和
     *Calculate the physical page number ppn, ppn is the sum of the number of pages in channel, chip, die, plane, block, page
     *****************************************************************************/
    i=0;
    while(i<channel)
    {
        ppn=ppn+page_channel[i];
        i++;
    }
    ppn=ppn+page_chip*chip+page_die*die+page_plane*plane+block*ssd->parameter->page_block+page;

    return ppn;
}

/********************************
 *函数功能是获得一个读子请求的状态
 *==============================
 *The function function is to get the status of a read sub request
 *********************************/
int set_entry_state(struct ssd_info *ssd,unsigned int lsn,unsigned int size)
{
    int temp,state,move;

    temp=~(0xffffffff<<size);
    move=lsn%ssd->parameter->subpage_page;
    state=temp<<move;

    return state;
}

/**************************************************
* Read request preprocessing function, when there is no data in the page read by the read request,
* It is necessary to preprocess the data written in this page to ensure that the data can be read.
 ***************************************************/
struct ssd_info *pre_process_page(struct ssd_info *ssd)
{
    int fl=0;
    unsigned int device,lsn,size,ope,lpn,full_page;
    unsigned int largest_lsn,sub_size,ppn,add_size=0;
    unsigned int i=0,j,k;
    int map_entry_new,map_entry_old,modify;
    int flag=0;
    char buffer_request[200];
    struct local *location;
    int64_t time;

    printf("\n");
    printf("begin pre_process_page.................\n");

    ssd->tracefile=fopen(ssd->tracefilename,"r");
    if(ssd->tracefile == NULL )      /*打开trace文件从中读取请求*/
    {
        printf("the trace file can't open\n");
        return NULL;
    }

    full_page=~(0xffffffff<<(ssd->parameter->subpage_page));
    printf("full page %d %d \n",full_page, ssd->parameter->subpage_page);
    /*计算出这个ssd的最大逻辑扇区号 | Calculate the maximum logical sector number of this ssd*/
    largest_lsn=(unsigned int )((ssd->parameter->chip_num*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->subpage_page)*(1-ssd->parameter->overprovide));
    printf("largest lsn : %d\n", largest_lsn);

    while(fgets(buffer_request,200,ssd->tracefile))
    {
        sscanf(buffer_request,"%lld %d %d %d %d",&time,&device,&lsn,&size,&ope);
        fl++;
        trace_assert(time,device,lsn,size,ope);                         /*断言，当读到的time，device，lsn，size，ope不合法时就会处理*/

        add_size=0;                                                     /* add_size is the size that this request has been preprocessed*/

        // Only pre process page that will be read
        if(ope==1)                                                      /* This is just a preprocessing of the read request, and the information in the corresponding position needs to be modified in advance.*/
        {
            while(add_size<size)
            {				
                lsn=lsn%largest_lsn;                                    /*Prevent lsn from getting bigger than the largest lsn*/		
                sub_size=ssd->parameter->subpage_page-(lsn%ssd->parameter->subpage_page);		
                if(add_size+sub_size>=size)                             /* This happens only when the size of a request is less than the size of a page or when the last page of a request is processed.*/
                {		
                    sub_size=size-add_size;		
                    add_size+=sub_size;		
                }

                if((sub_size>ssd->parameter->subpage_page)||(add_size>size))/*When preprocessing a sub-size, if the size is larger than a page or the processed size is larger than size, an error will be reported*/	
                {		
                    printf("pre_process sub_size:%d\n",sub_size);		
                }

                /*******************************************************************************************************
                 *利用逻辑扇区号lsn计算出逻辑页号lpn
                 *判断这个dram中映射表map中在lpn位置的状态
                 *A，这个状态==0，表示以前没有写过，现在需要直接将ub_size大小的子页写进去写进去
                 *B，这个状态>0，表示，以前有写过，这需要进一步比较状态，因为新写的状态可以与以前的状态有重叠的扇区的地方
                 *======================================================================================================
                 *Calculate the logical page number lpn using the logical sector number lsn
                 *Determine the state of the lpn position in the map table in this dram
                 *A, this state == 0, indicating that it has not been written before, now you need to directly write the sub-size sub-page into it and write it in.
                 *B, this state > 0, indicating that there has been a previous write, this requires further comparison of the state, because the newly written state can have overlapping sectors with the previous state.
                 ********************************************************************************************************/
                lpn=lsn/ssd->parameter->subpage_page;

                if(ssd->dram->map->map_entry[lpn].state==0)
                 
                {
                    /**************************************************************
                     *Get the ppn by using the get_ppn_for_pre_process function, and then get the location
                     * Modify the relevant parameters of ssd, the mapping table map of dram, and the status of the page under location
                     *=============================================================
                     *Get the get_ppn_for_pre_process function to get ppn, then get the location
                     *Modify the relevant parameters of ssd, the mapping table of dram, and the status of the page under location
                     ***************************************************************/
                    ppn=get_ppn_for_pre_process(ssd,lsn);                  
                    location=find_location(ssd,ppn);
                    // ssd->program_count++;
                    // ssd->in_program_size+=ssd->parameter->subpage_page;

                        ssd->channel_head[location->channel].program_count++;
                        ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		
                        ssd->dram->map->map_entry[lpn].pn=ppn;	
                        ssd->dram->map->map_entry[lpn].state=set_entry_state(ssd,lsn,sub_size);   //0001

                        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=lpn;
                        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=ssd->dram->map->map_entry[lpn].state;
                        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~ssd->dram->map->map_entry[lpn].state)&full_page);
                        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].cached_page=ssd->dram->map->map_entry[lpn].state;
                        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].cached_pages_num++;   // C1

                    free(location);
                    location=NULL;
                    
                }

                    
                else if(ssd->dram->map->map_entry[lpn].state>0)           /* state is not 0 */  //change_1
                {
                    map_entry_new=set_entry_state(ssd,lsn,sub_size);      /*Get the new state, and get a state with the original state*/

                    map_entry_old=ssd->dram->map->map_entry[lpn].state;
                    modify=map_entry_new|map_entry_old;
                    ppn=ssd->dram->map->map_entry[lpn].pn;
                    location=find_location(ssd,ppn);

                    // ssd->program_count++;
                    // ssd->in_program_size+=ssd->parameter->subpage_page;
                    
                    //change_1
                    //if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].cached_page==0)
                    //{
                            ssd->channel_head[location->channel].program_count++;
                            ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		
                            ssd->dram->map->map_entry[lsn/ssd->parameter->subpage_page].state=modify; 
                            ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=modify;
                            ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~modify)&full_page);
                            ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].cached_page=modify;
                            ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].cached_pages_num++;   //change_1

                            free(location);
                            location=NULL;
                   // }
                    
                    
                }//else if(ssd->dram->map->map_entry[lpn].state>0)
                
                lsn=lsn+sub_size;                                         /*下个子请求的起始位置*/
                add_size+=sub_size;                                       /*已经处理了的add_size大小变化*/
            }
            //while(add_size<size)
        }//if(ope==1) 
    }	

    printf("\n");
    printf("pre_process is complete!\n");

    fclose(ssd->tracefile);
    printf("C1");
    for(i=0;i<ssd->parameter->channel_number;i++)
        for(j=0;j<ssd->parameter->die_chip;j++)
            for(k=0;k<ssd->parameter->plane_die;k++)
            {
                fprintf(ssd->outputfile,"chip:%d,die:%d,plane:%d have free page: %d\n",i,j,k,ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].free_page);				
                fflush(ssd->outputfile);
            }

    return ssd;
}

/**************************************
*  The function is to obtain the physical page number ppn for the preprocessing function
 * Obtaining the page number is divided into dynamic acquisition and static acquisition
 **************************************/
unsigned int get_ppn_for_pre_process(struct ssd_info *ssd,unsigned int lsn)     
{
    unsigned int channel=0,chip=0,die=0,plane=0; 
    unsigned int ppn,lpn;
    unsigned int active_block;
    unsigned int channel_num=0,chip_num=0,die_num=0,plane_num=0;

#ifdef DEBUG
    printf("enter get_psn_for_pre_process\n");
#endif

    channel_num=ssd->parameter->channel_number;
    chip_num=ssd->parameter->chip_channel[0];
    die_num=ssd->parameter->die_chip;
    plane_num=ssd->parameter->plane_die;
    lpn=lsn/ssd->parameter->subpage_page;

    if (ssd->parameter->allocation_scheme==0)                /*Get ppn in dynamic mode*/ 
    {
        if (ssd->parameter->dynamic_allocation==0)           /*Indicates that in the full dynamic mode, that is, channel, chip, die, plane, block, etc. are all dynamically allocated*/
        {
            channel=ssd->token;
            ssd->token=(ssd->token+1)%ssd->parameter->channel_number;
            chip=ssd->channel_head[channel].token;
            ssd->channel_head[channel].token=(chip+1)%ssd->parameter->chip_channel[0];
            die=ssd->channel_head[channel].chip_head[chip].token;
            ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
            plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
            ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
        } 
        else if (ssd->parameter->dynamic_allocation==1)       /*Indicates semi-dynamic mode, channel is given statically, package, die, plane are allocated dynamically*/               
        {
            channel=lpn%ssd->parameter->channel_number;
            chip=ssd->channel_head[channel].token;
            ssd->channel_head[channel].token=(chip+1)%ssd->parameter->chip_channel[0];
            die=ssd->channel_head[channel].chip_head[chip].token;
            ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
            plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
            ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
        }
    } 
    else if (ssd->parameter->allocation_scheme==1)             /* Indicates static allocation, and there are 6 different static allocation methods of 0, 1, 2, 3, 4, 5 */
    {
        switch (ssd->parameter->static_allocation)
        {

            case 0:         
                {
                    channel=(lpn/(plane_num*die_num*chip_num))%channel_num;
                    chip=lpn%chip_num;
                    die=(lpn/chip_num)%die_num;
                    plane=(lpn/(die_num*chip_num))%plane_num;
                    break;
                }
            case 1:
                {
                    channel=lpn%channel_num;
                    chip=(lpn/channel_num)%chip_num;
                    die=(lpn/(chip_num*channel_num))%die_num;
                    plane=(lpn/(die_num*chip_num*channel_num))%plane_num;

                    break;
                }
            case 2:
                {
                    channel=lpn%channel_num;
                    chip=(lpn/(plane_num*channel_num))%chip_num;
                    die=(lpn/(plane_num*chip_num*channel_num))%die_num;
                    plane=(lpn/channel_num)%plane_num;
                    break;
                }
            case 3:
                {
                    channel=lpn%channel_num;
                    chip=(lpn/(die_num*channel_num))%chip_num;
                    die=(lpn/channel_num)%die_num;
                    plane=(lpn/(die_num*chip_num*channel_num))%plane_num;
                    break;
                }
            case 4:  
                {
                    channel=lpn%channel_num;
                    chip=(lpn/(plane_num*die_num*channel_num))%chip_num;
                    die=(lpn/(plane_num*channel_num))%die_num;
                    plane=(lpn/channel_num)%plane_num;

                    break;
                }
            case 5:   
                {
                    channel=lpn%channel_num;
                    chip=(lpn/(plane_num*die_num*channel_num))%chip_num;
                    die=(lpn/channel_num)%die_num;
                    plane=(lpn/(die_num*channel_num))%plane_num;

                    break;
                }
            default : return 0;
        }
    }

    /******************************************************************************
     *根据上述分配方法找到channel，chip，die，plane后，再在这个里面找到active_block
     *接着获得ppn
     *After finding the channel, chip, die, and plane according to the above allocation method, 
     *find the active_block in this and then get the ppn.
     ******************************************************************************/
    if(find_active_block(ssd,channel,chip,die,plane)==FAILURE)
    {
        printf("the read operation is expand the capacity of SSD\n");	
        return 0;
    }

    active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
    if(write_page(ssd,channel,chip,die,plane,active_block,&ppn)==ERROR)
    {
        return 0;
    }

    return ppn;
}


/***************************************************************************************************
 *函数功能是在所给的channel，chip，die，plane里面找到一个active_block然后再在这个block里面找到一个页，
 *再利用find_ppn找到ppn。
 * Function is to find an active_block in the given channel, chip, die, plane and then find a page in this block,
 * Use find_ppn to find ppn.
 ****************************************************************************************************/

struct ssd_info *get_ppn(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,struct sub_request *sub)
{
    int old_ppn=-1;
    unsigned int ppn,lpn,full_page;
    unsigned int active_block;
    unsigned int block;
    unsigned int page,flag=0,flag1=0;
    unsigned int old_state=0,state=0,copy_subpage=0;
    unsigned int is_in_tw=0, is_gc_inited=1;
    struct local *location;
    struct direct_erase *direct_erase_node,*new_direct_erase;
    struct gc_operation *gc_node;

    unsigned int i=0,j=0,k=0,l=0,m=0,n=0;

#ifdef DEBUG
    printf("enter get_ppn,channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);
#endif

    full_page=~(0xffffffff<<(ssd->parameter->subpage_page));
    lpn=sub->lpn;

    /*************************************************************************************
     *利用函数find_active_block在channel，chip，die，plane找到活跃block
     *并且修改这个channel，chip，die，plane，active_block下的last_write_page和free_page_num
     * Use the find_active_block function to find active blocks on channel, chip, die, plane
     * and modify the last_write_page and free_page_num under this channel, chip, die, plane, active_block
     **************************************************************************************/
    if(find_active_block(ssd,channel,chip,die,plane)==FAILURE)                      
    {
        printf("ERROR :there is no free page in channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);	
        return ssd;
    }

    active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page++;	
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;

    if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page>=ssd->parameter->page_block)
    {
        printf("error! the last write page larger than %d!!\n", ssd->parameter->page_block);
        while(1){}
    }

    block=active_block;	
    page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;	

    if(ssd->dram->map->map_entry[lpn].state==0)                                       /*this is the first logical page*/
    {
        if(ssd->dram->map->map_entry[lpn].pn!=0)
        {
            printf("Error in get_ppn()\n");
        }
        ssd->dram->map->map_entry[lpn].pn=find_ppn(ssd,channel,chip,die,plane,block,page);
        ssd->dram->map->map_entry[lpn].state=sub->state;
    }
    else                                                                            /*这个逻辑页进行了更新，需要将原来的页置为失效*/
    {  
        /*This logical page has been updated, and the original page needs to be invalidated*/
        ppn=ssd->dram->map->map_entry[lpn].pn;
        location=find_location(ssd,ppn);
        if(	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn!=lpn)
        {
            printf("\nError in get_ppn()\n");
        }

        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;             /*Indicates that a certain page is invalid, and both the valid and free states are marked as 0*/
        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;              /*Indicates that a certain page is invalid, and both the valid and free states are marked as 0*/
        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].cached_page=0;    //changes Done Here
        ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
            

        /*******************************************************************************************
         *该block中全是invalid的页，可以直接删除，就在创建一个可擦除的节点，挂在location下的plane下面
         *The block is all invalid pages, you can delete directly, just create an erasable node, hung under the plane under the location
         ********************************************************************************************/
        if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num==ssd->parameter->page_block)    
        {
            new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
            alloc_assert(new_direct_erase,"new_direct_erase");
            memset(new_direct_erase,0, sizeof(struct direct_erase));

            new_direct_erase->block=location->block;
            new_direct_erase->next_node=NULL;
            direct_erase_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
            if (direct_erase_node==NULL)
            {
                ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
            } 
            else
            {
                new_direct_erase->next_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
                ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
            }
        }

        free(location);
        location=NULL;
        ssd->dram->map->map_entry[lpn].pn=find_ppn(ssd,channel,chip,die,plane,block,page);
        ssd->dram->map->map_entry[lpn].state=(ssd->dram->map->map_entry[lpn].state|sub->state);
    }


    sub->ppn=ssd->dram->map->map_entry[lpn].pn;                                      /*修改sub子请求的ppn，location等变量*/
    sub->location->channel=channel;
    sub->location->chip=chip;
    sub->location->die=die;
    sub->location->plane=plane;
    sub->location->block=active_block;
    sub->location->page=page;

    ssd->program_count++;                                                           /*修改ssd的program_count,free_page等变量*/
    ssd->in_program_size+=ssd->parameter->subpage_page;
    ssd->channel_head[channel].program_count++;
    ssd->channel_head[channel].chip_head[chip].program_count++;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].lpn=lpn;	
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state=sub->state;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state=((~(sub->state))&full_page);
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].cached_page=sub->state;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].written_count++;
    ssd->write_flash_count++;

    if (ssd->parameter->active_write==0)                                            /* If there is no active policy, only gc_hard_threshold is used, and the GC process cannot be interrupted.*/
    {                                                                               /* If the number of free_pages in the plane is less than the threshold set by gc_hard_threshold, a gc operation will be generated*/
        if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold))
        {
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

            // only initialized gc if it wasn't initialized previously
            if (is_gc_inited) {
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

    return ssd;
}
/*****************************************************************************************
*The function of this function is to find a new ppn for the gc operation, because in the gc operation, a new physical block needs to be found to store the data on the original physical block
 * The function of finding new physical blocks in GC, which will not cause cyclic GC operation
 ******************************************************************************************/

unsigned int get_ppn_for_gc(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)     
{
    unsigned int ppn;
    unsigned int active_block,block,page;

#ifdef DEBUG
    printf("enter get_ppn_for_gc,channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);
#endif

    if(find_active_block(ssd,channel,chip,die,plane)!=SUCCESS)
    {
        printf("\n\n Error int get_ppn_for_gc().\n");
        return 0xffffffff;
    }

    active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;

    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page++;	
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;

    if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page >= ssd->parameter->page_block)
    {
        printf("error! the last write page larger than %d!!\n", ssd->parameter->page_block);
        while(1){}
    }

    block=active_block;	
    page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;	

    ppn=find_ppn(ssd,channel,chip,die,plane,block,page);

    ssd->program_count++;
    ssd->in_program_size+=ssd->parameter->subpage_page;
    ssd->channel_head[channel].program_count++;
    ssd->channel_head[channel].chip_head[chip].program_count++;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].written_count++;
    ssd->write_flash_count++;

    return ppn;

}

/*********************************************************************************************************************
* Revised by Zhu Zhiming on July 28, 2011
 *The function of the function is the erase_operation erase operation, which erases the blocks under the channel, chip, die, and plane
 *That is to initialize the relevant parameters of this block, eg: free_page_num=page_block, invalid_page_num=0, last_write_page=-1, erase_count++
 *The relevant parameters of each page under this block should also be modified。
 *********************************************************************************************************************/

Status erase_operation(struct ssd_info * ssd,unsigned int channel ,unsigned int chip ,unsigned int die ,unsigned int plane ,unsigned int block)
{
    unsigned int i=0;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num=ssd->parameter->page_block;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num=0;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page=-1;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].erase_count++;

    for (i=0;i<ssd->parameter->page_block;i++)
    {
        ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state=PG_SUB;
        ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state=0;
        ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].cached_page=0;
        ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn=-1;
    }
    ssd->erase_count++;
    ssd->channel_head[channel].erase_count++;			
    ssd->channel_head[channel].chip_head[chip].erase_count++;
    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page+=ssd->parameter->page_block;

    return SUCCESS;

}


/**************************************************************************************
 *这个函数的功能是处理INTERLEAVE_TWO_PLANE，INTERLEAVE，TWO_PLANE，NORMAL下的擦除的操作。
 ***************************************************************************************/
Status erase_planes(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die1, unsigned int plane1,unsigned int command)
{
    unsigned int die=0;
    unsigned int plane=0;
    unsigned int block=0;
    struct direct_erase *direct_erase_node=NULL;
    unsigned int block0=0xffffffff;
    unsigned int block1=0;

    if((ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node==NULL)||               
            ((command!=INTERLEAVE_TWO_PLANE)&&(command!=INTERLEAVE)&&(command!=TWO_PLANE)&&(command!=NORMAL)))     /*如果没有擦除操作，或者command不对，返回错误*/           
    {
        return ERROR;
    }

    /************************************************************************************************************
     *处理擦除操作时，首先要传送擦除命令，这是channel，chip处于传送命令的状态，即CHANNEL_TRANSFER，CHIP_ERASE_BUSY
     *下一状态是CHANNEL_IDLE，CHIP_IDLE。
     *************************************************************************************************************/
    block1=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node->block;

    ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
    ssd->channel_head[channel].current_time=ssd->current_time;										
    ssd->channel_head[channel].next_state=CHANNEL_IDLE;	

    ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;										
    ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
    ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;

    if(command==INTERLEAVE_TWO_PLANE)                                       /*高级命令INTERLEAVE_TWO_PLANE的处理*/
    {
        for(die=0;die<ssd->parameter->die_chip;die++)
        {
            block0=0xffffffff;
            if(die==die1)
            {
                block0=block1;
            }
            for (plane=0;plane<ssd->parameter->plane_die;plane++)
            {
                direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
                if(direct_erase_node!=NULL)
                {

                    block=direct_erase_node->block; 

                    if(block0==0xffffffff)
                    {
                        block0=block;
                    }
                    else
                    {
                        if(block!=block0)
                        {
                            continue;
                        }

                    }
                    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=direct_erase_node->next_node;
                    erase_operation(ssd,channel,chip,die,plane,block);     /*真实的擦除操作的处理*/
                    free(direct_erase_node);                               
                    direct_erase_node=NULL;
                    ssd->direct_erase_count++;
                }

            }
        }

        ssd->interleave_mplane_erase_count++;                             /*发送了一个interleave two plane erase命令,并计算这个处理的时间，以及下一个状态的时间*/
        ssd->channel_head[channel].next_state_predict_time=ssd->current_time+18*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tWB;       
        ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time-9*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tBERS;

    }
    else if(command==INTERLEAVE)                                          /*高级命令INTERLEAVE的处理*/
    {
        for(die=0;die<ssd->parameter->die_chip;die++)
        {
            for (plane=0;plane<ssd->parameter->plane_die;plane++)
            {
                if(die==die1)
                {
                    plane=plane1;
                }
                direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
                if(direct_erase_node!=NULL)
                {
                    block=direct_erase_node->block;
                    ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=direct_erase_node->next_node;
                    erase_operation(ssd,channel,chip,die,plane,block);
                    free(direct_erase_node);
                    direct_erase_node=NULL;
                    ssd->direct_erase_count++;
                    break;
                }	
            }
        }
        ssd->interleave_erase_count++;
        ssd->channel_head[channel].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;       
        ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
    }
    else if(command==TWO_PLANE)                                          /*高级命令TWO_PLANE的处理*/
    {

        for(plane=0;plane<ssd->parameter->plane_die;plane++)
        {
            direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane].erase_node;
            if((direct_erase_node!=NULL))
            {
                block=direct_erase_node->block;
                if(block==block1)
                {
                    ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane].erase_node=direct_erase_node->next_node;
                    erase_operation(ssd,channel,chip,die1,plane,block);
                    free(direct_erase_node);
                    direct_erase_node=NULL;
                    ssd->direct_erase_count++;
                }
            }
        }

        ssd->mplane_erase_conut++;
        ssd->channel_head[channel].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;      
        ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
    }
    else if(command==NORMAL)                                             /*普通命令NORMAL的处理*/
    {
        direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node;
        block=direct_erase_node->block;
        ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node=direct_erase_node->next_node;
        free(direct_erase_node);
        direct_erase_node=NULL;
        erase_operation(ssd,channel,chip,die1,plane1,block);

        ssd->direct_erase_count++;
        ssd->channel_head[channel].next_state_predict_time=ssd->current_time+5*ssd->parameter->time_characteristics.tWC;       								
        ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tWB+ssd->parameter->time_characteristics.tBERS;	
    }
    else
    {
        return ERROR;
    }

    direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node;

    if(((direct_erase_node)!=NULL)&&(direct_erase_node->block==block1))
    {
        return FAILURE; 
    }
    else
    {
        return SUCCESS;
    }
}


/*******************************************************************************************************************
 * The GC operation is triggered when the free block of a certain plane is less than the threshold. When a certain plane is triggered, the GC operation occupies the die where the plane is located, because the die is an independent unit.
 * For the GC operation of a die, try to erase four planes at the same time, and use the interleave erase operation. GC operations should be able to be stopped at any time (moving data and erasing
 * Not available, but the gap time can stop the GC operation) to serve the newly arrived request, when the request service is completed, use the request gap time to continue the GC operation. Can set two
 * GC thresholds, a soft threshold and a hard threshold. The soft threshold means that after reaching the threshold, the active GC operation can be started, and the GC can be interrupted by the new request by using the intermittent time;
 * When the hard threshold is reached, the GC operation is mandatory, and this GC operation cannot be interrupted until it returns above the hard threshold.
 * In this function, find out whether there is a block that can be deleted directly in all the planes of the die. If so, use the interleave two plane command to delete
 * These blocks, otherwise how many planes have such directly deleted blocks will be deleted at the same time, if not, the worst is to delete this plane alone, if even this is not satisfied,
 * Jump out directly to the gc_parallelism function for further GC operations. This function looks for blocks that are all invalid and deletes them directly. If it finds a block that can be deleted directly, it returns 1. If it does not find
 * to return -1.
 *===================================================================================================================
 *The GC operation is triggered by the free block of a plane being less than the threshold. When a plane is triggered, the GC operation occupies the die where the plane is located, because the die is a separate unit.
 *For a GC operation of a die, try to achieve four plane simultaneous erase, using the interleave erase operation. GC operations should be able to stop at any time (moving data and erasing
 *********************************************************************************************************************/

int gc_direct_erase(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)     
{
    unsigned int lv_die=0,lv_plane=0;                                                           /*为避免重名而使用的局部变量 Local variables*/
    unsigned int interleaver_flag=FALSE,muilt_plane_flag=FALSE;
    unsigned int normal_erase_flag=TRUE;

    struct direct_erase * direct_erase_node1=NULL;
    struct direct_erase * direct_erase_node2=NULL;

    direct_erase_node1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
    if (direct_erase_node1==NULL)
    {
        return FAILURE;
    }

    /********************************************************************************************************
     *当能处理TWOPLANE高级命令时，就在相应的channel，chip，die中两个不同的plane找到可以执行TWOPLANE操作的block
     *并置muilt_plane_flag为TRUE
     *********************************************************************************************************/
    if((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)
    {	
        for(lv_plane=0;lv_plane<ssd->parameter->plane_die;lv_plane++)
        {
            direct_erase_node2=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
            if((lv_plane!=plane)&&(direct_erase_node2!=NULL))
            {
                if((direct_erase_node1->block)==(direct_erase_node2->block))
                {
                    muilt_plane_flag=TRUE;
                    break;
                }
            }
        }
    }

    /***************************************************************************************
     *当能处理INTERLEAVE高级命令时，就在相应的channel，chip找到可以执行INTERLEAVE的两个block
     *并置interleaver_flag为TRUE
     ****************************************************************************************/
    if((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)
    {
        for(lv_die=0;lv_die<ssd->parameter->die_chip;lv_die++)
        {
            if(lv_die!=die)
            {
                for(lv_plane=0;lv_plane<ssd->parameter->plane_die;lv_plane++)
                {
                    if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node!=NULL)
                    {
                        interleaver_flag=TRUE;
                        break;
                    }
                }
            }
            if(interleaver_flag==TRUE)
            {
                break;
            }
        }
    }

    /************************************************************************************************************************
     *A，如果既可以执行twoplane的两个block又有可以执行interleaver的两个block，那么就执行INTERLEAVE_TWO_PLANE的高级命令擦除操作
     *B，如果只有能执行interleaver的两个block，那么就执行INTERLEAVE高级命令的擦除操作
     *C，如果只有能执行TWO_PLANE的两个block，那么就执行TWO_PLANE高级命令的擦除操作
     *D，没有上述这些情况，那么就只能够执行普通的擦除操作了
     *************************************************************************************************************************/
    if ((muilt_plane_flag==TRUE)&&(interleaver_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))     
    {
        if(erase_planes(ssd,channel,chip,die,plane,INTERLEAVE_TWO_PLANE)==SUCCESS)
        {
            return SUCCESS;
        }
    } 
    else if ((interleaver_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))
    {
        if(erase_planes(ssd,channel,chip,die,plane,INTERLEAVE)==SUCCESS)
        {
            return SUCCESS;
        }
    }
    else if ((muilt_plane_flag==TRUE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
    {
        if(erase_planes(ssd,channel,chip,die,plane,TWO_PLANE)==SUCCESS)
        {
            return SUCCESS;
        }
    }

    if (normal_erase_flag==TRUE)                              /*不是每个plane都有可以直接删除的block，只对当前plane进行普通的erase操作，或者只能执行普通命令*/
    {
        if (erase_planes(ssd,channel,chip,die,plane,NORMAL)==SUCCESS)
        {
            return SUCCESS;
        } 
        else
        {
            return FAILURE;                                     /*目标的plane没有可以直接删除的block，需要寻找目标擦除块后在实施擦除操作*/
        }
    }
    return SUCCESS;
}


Status move_page(struct ssd_info * ssd, struct local *location, unsigned int * transfer_size)
{
    struct local *new_location=NULL;
    unsigned int free_state=0,valid_state=0,cached_page=0;
    unsigned int lpn=0,old_ppn=0,ppn=0;

    lpn=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn;
    cached_page=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].cached_page;
    valid_state=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state;
    free_state=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state;
    old_ppn=find_ppn(ssd,location->channel,location->chip,location->die,location->plane,location->block,location->page);     /*Record the ppn of this effective mobile page, compare the ppn in the map or the additional mapping relationship, and perform deletion and addition operations*/

    ppn=get_ppn_for_gc(ssd,location->channel,location->chip,location->die,location->plane);                /*The found ppn must be in the plane where the gc operation occurs, so that the copyback operation can be used to obtain the ppn for the gc operation*/


    new_location=find_location(ssd,ppn);                                                                   /*Get new_location according to the newly obtained ppn*/


    if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
    {
        if (ssd->parameter->greed_CB_ad==1)                                                                /* Greedily use advanced commands */
        {
            ssd->copy_back_count++;
            ssd->gc_copy_back++;
            while (old_ppn%2!=ppn%2)
            {
                //if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].cached_page==1)  //changes_1
                //{
                ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state=0;
                ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn=0;
                ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state=0;
                ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].cached_page=0;
                ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].invalid_page_num++;
                ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].cached_pages_num--; //changes_1

                free(new_location);
                new_location=NULL;
               // }
                ppn=get_ppn_for_gc(ssd,location->channel,location->chip,location->die,location->plane);    /* The found ppn must be in the plane where the gc operation occurs, and it must meet the parity address limit to use the copyback operation*/
                ssd->program_count--;
                ssd->in_program_size-=ssd->parameter->subpage_page;
                ssd->write_flash_count--;
                ssd->waste_page_count++;
            }

            if(new_location==NULL)
            {
                new_location=find_location(ssd,ppn);
            }

            //if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].cached_page==0)  //changes_1
            //{
            ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state=free_state;
            ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn=lpn;
            ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state=valid_state;
            ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].cached_page=cached_page;
            ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].cached_pages_num++;
           // }
        } 

        else
        {
            if (old_ppn%2!=ppn%2)
            {
                (* transfer_size)+=size(valid_state);
            }
            else
            {

                ssd->copy_back_count++;
                ssd->gc_copy_back++;
            }
        }	
    } 
    else
    {
        (* transfer_size)+=size(valid_state);
    }
    //new location 
    ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state=free_state;
    ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn=lpn;
    ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state=valid_state;
    ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].cached_page=cached_page;
    ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].cached_pages_num++;

    //old location 
    ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;
    ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
    ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;
    ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].cached_page=0;
    ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
    
    if (old_ppn==ssd->dram->map->map_entry[lpn].pn)                                                     /*修改映射表*/
    {
        ssd->dram->map->map_entry[lpn].pn=ppn;
    }

    free(new_location);
    new_location=NULL;

    return SUCCESS;
}

/*******************************************************************************************************************************************
*  The target plane does not have a block that can be deleted directly. It is necessary to find the target erase block before performing the erase operation. It is used in uninterruptible gc operations. If a block is successfully deleted, it returns 1, and if a block is not deleted, it returns -1
 * In this function, regardless of whether the target channel or die is free, erase the block with the most invalid_page_num.
 ********************************************************************************************************************************************/

int uninterrupt_gc(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane, struct gc_operation *gc_node) 
{
    // printf("U_GC");
    unsigned int i=0,invalid_page=0;
    unsigned int block,active_block,transfer_size,free_page,page_move_count=0;                           /*Record the block number with the most failed pages*/
    struct local *  location=NULL;
    unsigned int total_invalid_page_num=0;
    unsigned int total_cached_page_num=0;

        
    if(find_active_block(ssd,channel,chip,die,plane)!=SUCCESS)                                           /* get active block */
    {
        printf("\n\n Error in uninterrupt_gc().\n");
        return ERROR;
    }
    active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;

    invalid_page=0;
    transfer_size=0;
    int cached_page=0;
    block=-1;

    FILE *fp;
    char filename[] = "Block_count.txt";
    fp = fopen(filename, "w");


    for(i=0;i<ssd->parameter->block_plane;i++)               /*Find the block number with the most invalid_page, and the largest invalid_page_num*/
    {	


        // int invalid_value=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;

        // if(fp)
        //     {
        //         fprintf(fp, "Block Number = %d ------------------------------------------Invalid Pages = %d\n",i, invalid_value);
        //     }

        
        // total_invalid_page_num+=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;

        // if((active_block!=i)&&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num>invalid_page))						
        // {				
        //     invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
        //     block=i;

        // }

         int cache_value=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].cached_pages_num;

         if(fp)
         {
            fprintf(fp, "Block Number = %d ------------------------------------------Cached Pages = %d\n",i, cache_value);
         }

        total_cached_page_num+=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].cached_pages_num;

        if((active_block!=i)&&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].cached_pages_num > cached_page))						
        {				
            cached_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].cached_pages_num;
            block=i;
           
        }
        
    }

    if(fp)
    {
        fprintf(fp, "Block number= %d is Selected as Victim Block\n",block);
    }	

    fclose(fp);

    if(block==-1)
    {
        return 1;
    }

    // if(invalid_page<5)
    // {
    // printf("\ntoo less invalid page. \t %d\t %d\t%d\t%d\t%d\t%d\t\n",invalid_page,channel,chip,die,plane,block);
    // }
    

    free_page=0;
    for(i=0;i<ssd->parameter->page_block;i++)		                                                     /*Check each page one by one, if the page with valid data needs to be moved to other places for storage*/	
    {		
        if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state&PG_SUB)==0x0000000f)
        {
            free_page++;
        }
        if(free_page!=0)
        {
            printf("\ntoo much free page. \t %d\t .%d\t%d\t%d\t%d\t\n",free_page,channel,chip,die,plane);
        }

        if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state>0)  /*This page is a valid page and requires copyback operation*/		
        {	
            location=(struct local * )malloc(sizeof(struct local ));
            alloc_assert(location,"location");
            memset(location,0, sizeof(struct local));

            location->channel=channel;
            location->chip=chip;
            location->die=die;
            location->plane=plane;
            location->block=block;
            location->page=i;
            move_page(ssd, location, &transfer_size);                                                   /*真实的move_page操作*/
            page_move_count++;

            free(location);	
            location=NULL;
        }				
    }

    erase_operation(ssd,channel ,chip , die,plane ,block);	                                         /*After the move_page operation is executed, the erase operation of the block is executed immediately*/

    ssd->channel_head[channel].current_state=CHANNEL_GC;									
    ssd->channel_head[channel].current_time=ssd->current_time;										
    ssd->channel_head[channel].next_state=CHANNEL_IDLE;	
    ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;								
    ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
    ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;			

    /***************************************************************
  *In the two cases where the COPYBACK advanced command can be executed and the COPYBACK advanced command cannot be executed,
     * Calculation of the next state time of the channel, and calculation of the next state time of the chip
     ***************************************************************/
    if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
    {
        if (ssd->parameter->greed_CB_ad==1)
        {
            ssd->channel_head[channel].next_state_predict_time=ssd->current_time+page_move_count*(7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tPROG);			
            ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
        } 
    } 
    else
    {

        ssd->channel_head[channel].next_state_predict_time=ssd->current_time+page_move_count*(7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tPROG)+transfer_size*SECTOR*(ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tRC);
        ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;
    }

    gc_node->x_start_time = ssd->current_time;
    gc_node->x_moved_pages = page_move_count;
    gc_node->x_end_time = ssd->channel_head[channel].next_state_predict_time;

    return 1;
}


/*******************************************************************************************************************************************
*  The target plane does not have a block that can be deleted directly. It needs to find the target erase block and then implement the erase operation. It is used in the gc operation that can be interrupted. If a block is successfully deleted, it returns 1, and if a block is not deleted, it returns -1
 * In this function, it is not necessary to consider whether the target channel, die is free or not
 ********************************************************************************************************************************************/

int interrupt_gc(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,struct gc_operation *gc_node)        
{
    // printf("I_GC");
    unsigned int i,block,active_block,transfer_size,invalid_page=0, cached_page=0;
    struct local *location;

    active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
    transfer_size=0;

    FILE *fp;
    char filename[] = "Block_count.txt";
    fp = fopen(filename, "w");
    
    //fprintf(fp, "Block_Number ---------------------Invalid_Pages----------------------Cached_Pages\n");
    // if(gc_node->block>=ssd->parameter->block_plane)
    // {
    //     for(i=0;i<ssd->parameter->block_plane;i++)
    //     {	

    //             invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
    //             cached_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].cached_pages_num;
    //             fprintf(fp, "Block->%d ---------------------%d ----------------------%d\n",i, invalid_page,cached_page);						
                
    //     }
    // }

    if (gc_node->block>=ssd->parameter->block_plane)
    {
        for(i=0;i<ssd->parameter->block_plane;i++)
        {	
            int invalid_value=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;

        // if(fp)
        //     printf("Check 7777\n"),
        //     fprintf(fp, "Block Number = %d ------------------------------------------Invalid Pages = %d\n",i, invalid_value);
            	
        //     if((active_block!=i)&&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num > invalid_page))						
        //     {				
        //         invalid_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;	
        //         block=i;
        //         //fprintf(fp, "Block Number = %d ------------------------------------------Invalid Pages = %d\n",block, invalid_page);						
        //     }

           int cache_value=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].cached_pages_num;
        
         if(fp)
            fprintf(fp, "Block Number = %d ------------------------------------------Cached Pages = %d\n",i, cache_value); 
        
            if((active_block!=i)&&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].cached_pages_num>cached_page))						
            {				
                cached_page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].cached_pages_num;
                block=i;
                //if(fp)
                printf("chck 5555\n");
                //fprintf(fp, "Block Number = %d ------------------------------------------Cached Pages = %d\n",block, cached_page);						
						
            }
         
        }
        // if(fp)
        // printf("chck 777777\n"),
        // fprintf(fp, "Block-> %d is Selected as Victim Block\n", block);

        gc_node->block=block;
    }

    if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].invalid_page_num!=ssd->parameter->page_block)     /* also need to perform copyback operation */
    {
        for (i=gc_node->page;i<ssd->parameter->page_block;i++)
        {
            if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].page_head[i].valid_state>0) 
            {
                location=(struct local * )malloc(sizeof(struct local ));
                alloc_assert(location,"location");
                memset(location,0, sizeof(struct local));

                location->channel=channel;
                location->chip=chip;
                location->die=die;
                location->plane=plane;
                location->block=block;
                location->page=i;
                transfer_size=0;

                move_page( ssd, location, &transfer_size);

                free(location);
                location=NULL;

                gc_node->page=i+1;
                ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].invalid_page_num++;
                ssd->channel_head[channel].current_state=CHANNEL_C_A_TRANSFER;									
                ssd->channel_head[channel].current_time=ssd->current_time;										
                ssd->channel_head[channel].next_state=CHANNEL_IDLE;	
                ssd->channel_head[channel].chip_head[chip].current_state=CHIP_COPYBACK_BUSY;								
                ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
                ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;		

                if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
                {					
                    ssd->channel_head[channel].next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+7*ssd->parameter->time_characteristics.tWC;		
                    ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
                } 
                else
                {	
                    ssd->channel_head[channel].next_state_predict_time=ssd->current_time+(7+transfer_size*SECTOR)*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(7+transfer_size*SECTOR)*ssd->parameter->time_characteristics.tWC;					
                    ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
                }
                
                gc_node->x_moved_pages = gc_node->x_moved_pages+1;

                return 0;    
            }
        }
    }
    else
    {
        erase_operation(ssd,channel ,chip, die,plane,gc_node->block);	

        ssd->channel_head[channel].current_state=CHANNEL_C_A_TRANSFER;									
        ssd->channel_head[channel].current_time=ssd->current_time;										
        ssd->channel_head[channel].next_state=CHANNEL_IDLE;								
        ssd->channel_head[channel].next_state_predict_time=ssd->current_time+5*ssd->parameter->time_characteristics.tWC;

        ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;								
        ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
        ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;							
        ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS;

        gc_node->x_end_time = ssd->channel_head[channel].next_state_predict_time;
        
        return 1;                                                                      /*该gc操作完成，返回1，可以将channel上的gc请求节点删除*/
    }

    printf("there is a problem in interrupt_gc\n");
    return 1;
}

/*************************************************************
 *函数的功能是当处理完一个gc操作时，需要把gc链上的gc_node删除掉
 *The function of the function is to delete the gc_node on the gc chain when processing a gc operation.
 **************************************************************/
int delete_gc_node(struct ssd_info *ssd, unsigned int channel,struct gc_operation *gc_node)
{
    struct gc_operation *gc_pre=NULL;
    if(gc_node==NULL)                                                                  
    {
        return ERROR;
    }

    if (gc_node==ssd->channel_head[channel].gc_command)
    {
        ssd->channel_head[channel].gc_command=gc_node->next_node;
    }
    else
    {
        gc_pre=ssd->channel_head[channel].gc_command;
        while (gc_pre->next_node!=NULL)
        {
            if (gc_pre->next_node==gc_node)
            {
                gc_pre->next_node=gc_node->next_node;
                break;
            }
            gc_pre=gc_pre->next_node;
        }
    }

    // gc_node->x_end_time = ssd->channel_head[channel].next_state_predict_time;

    int64_t start_time = gc_node->x_start_time;
    int64_t end_time = gc_node->x_end_time;
    unsigned int moved_page = gc_node->x_moved_pages;
    double free_page_percent = gc_node->x_free_percentage;

    if (moved_page != 0) {
        printf("gc-disk-%u: %2d %2d %2d %2d %6.2f %4u %16lld %16lld %16lld %12lld\n", ssd->diskid, channel, gc_node->chip, gc_node->die, gc_node->plane, free_page_percent, moved_page, gc_node->x_init_time, start_time, end_time, end_time-start_time);
        fprintf(ssd->outfile_gc, "%d \t %d \t %d \t %d \t%6.2f %8u %16lld %16lld %16lld | %lld %.3f %.3f %.3f %.3f | %lu\n", channel, gc_node->chip, gc_node->die, gc_node->plane, free_page_percent, moved_page, start_time, end_time, end_time-start_time, ssd->current_time, get_crt_free_block_prct(ssd), get_crt_free_page_prct(ssd), get_crt_nonempty_free_page_prct(ssd), get_crt_nonempty_free_block_prct(ssd), ssd->direct_erase_count);
        fflush(ssd->outfile_gc);
        ssd->num_gc++;
        ssd->gc_move_page += moved_page;
        if (ssd->gclock_pointer!=NULL && ssd->gclock_pointer->is_available == 0) {
            ssd->gclock_pointer->end_time = gc_node->x_end_time+RAID_SSD_LATENCY_NS*2;
            ssd->gclock_pointer->holder_id = -1;
            ssd->gclock_pointer->is_available = 1;
        } else if (ssd->gclock_pointer!=NULL && ssd->gclock_pointer->is_available == 1) {
            printf("Error! gclock availability supposed to be 0 \n");
            getchar();
        }

        if (end_time <= start_time) {
            printf("Error! GC end time is before or equal it's start time! %lld %lld %u\n", start_time, end_time, moved_page);
            while(1);
        }   
    }
    
    free(gc_node);
    gc_node=NULL;
    ssd->gc_request--;
    return SUCCESS;
}

/***************************************
 *这个函数的功能是处理channel的每个gc操作
 *This function handle each gc operation of the channel.
 ****************************************/

Status gc_for_channel(struct ssd_info *ssd, unsigned int channel)
{
    int flag_direct_erase=1,flag_gc=1,flag_invoke_gc=1;
    unsigned int chip,die,plane,flag_priority=0;
    unsigned int current_state=0, next_state=0;
    long long next_state_predict_time=0;
    struct gc_operation *gc_node=NULL,*gc_p=NULL;
    int64_t temp_int64, upper_tw_limit;

    /*******************************************************************************************
     *查找每一个gc_node，获取gc_node所在的chip的当前状态，下个状态，下个状态的预计时间
     *如果当前状态是空闲，或是下个状态是空闲而下个状态的预计时间小于当前时间，并且是不可中断的gc
     *那么就flag_priority令为1，否则为0
     *==========================================================================================
     *Find each gc_node, get the current state of the chip where gc_node is located, the next state, the expected time of the next state
     *If the current state is idle, or the next state is idle and the expected time of the next state is less than the current time, and is uninterruptible gc
     *Then the flag_priority is 1, otherwise 0
     ********************************************************************************************/
    gc_node=ssd->channel_head[channel].gc_command;
    while (gc_node!=NULL)
    {
        current_state=ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
        next_state=ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
        next_state_predict_time=ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
        if((current_state==CHIP_IDLE)||((next_state==CHIP_IDLE)&&(next_state_predict_time<=ssd->current_time)))
        {
            if (gc_node->priority==GC_UNINTERRUPT)                                     /*这个gc请求是不可中断的，优先服务这个gc操作*/
            {
                flag_priority=1;
                break;
            }
        }
        gc_node=gc_node->next_node;
    }
    if (flag_priority!=1)                                                              /*没有找到不可中断的gc请求，首先执行队首的gc请求 | Did not find an uninterruptible gc request, first execute the gc request of the team leader*/
    {
        gc_node=ssd->channel_head[channel].gc_command;
        while (gc_node!=NULL)
        {
            current_state=ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
            next_state=ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
            next_state_predict_time=ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
            /**********************************************
             *需要gc操作的目标chip是空闲的，才可以进行gc操作
             *The target chip that needs the gc operation is idle before the gc operation can be performed.
             ***********************************************/
            if((current_state==CHIP_IDLE)||((next_state==CHIP_IDLE)&&(next_state_predict_time<=ssd->current_time)))   
            {
                break;
            }
            gc_node=gc_node->next_node;
        }

    }
    if(gc_node==NULL)
    {
        return FAILURE;
    }

    // check whether gcsync active or not. If active check whether 
    // GC can be started or not
    if (ssd->is_gcsync == 1 && ssd->gc_time_window != 0 && ssd->ndisk != 0) {
        temp_int64 = ssd->channel_head[channel].current_time / (ssd->gc_time_window + GCSSYNC_BUFFER_TIME);
        upper_tw_limit = temp_int64 * (ssd->gc_time_window + GCSSYNC_BUFFER_TIME) + ssd->gc_time_window;
        if (temp_int64 % ssd->ndisk != ssd->diskid || ssd->channel_head[channel].current_time > upper_tw_limit) {
            // Its not this disk turn to do GC
            return FAILURE;
        }
    }

    // check whether gclock active or not. If active check whether 
    // GC can be started or not
    if (ssd->is_gclock == 1) {
        if (ssd->gclock_pointer->is_available == 1 && ssd->gclock_pointer->end_time <= ssd->current_time) {
            ssd->current_time+=RAID_SSD_LATENCY_NS*4;
            ssd->gclock_pointer->is_available = 0;
            ssd->gclock_pointer->begin_time = ssd->current_time;
            ssd->gclock_pointer->holder_id = ssd->diskid;
        } else {
            return FAILURE;
        }
    }
    
    chip=gc_node->chip;
    die=gc_node->die;
    plane=gc_node->plane;

    gc_node->x_start_time = ssd->current_time;
    gc_node->x_end_time = ssd->current_time;
    gc_node->x_moved_pages = 0;
 
    if (gc_node->priority==GC_UNINTERRUPT)
    {
        flag_direct_erase=gc_direct_erase(ssd,channel,chip,die,plane);
        if (flag_direct_erase!=SUCCESS)
        {
            flag_gc=uninterrupt_gc(ssd,channel,chip,die,plane, gc_node);                         /*当一个完整的gc操作完成时（已经擦除一个块，回收了一定数量的flash空间），返回1，将channel上相应的gc操作请求节点删除*/
            if (flag_gc==1)
            {
                delete_gc_node(ssd,channel,gc_node);
            }
        }
        else
        {
            gc_node->x_end_time = ssd->channel_head[channel].next_state_predict_time;
            delete_gc_node(ssd,channel,gc_node);
        }
        return SUCCESS;
    }
    /*******************************************************************************
     *可中断的gc请求，需要首先确认该channel上没有子请求在这个时刻需要使用这个channel，
     *没有的话，在执行gc操作，有的话，不执行gc操作
     ********************************************************************************/
    else        
    {
        flag_invoke_gc=decide_gc_invoke(ssd,channel);                                  /*判断是否有子请求需要channel，如果有子请求需要这个channel，那么这个gc操作就被中断了*/

        if (flag_invoke_gc==1)
        {
            flag_direct_erase=gc_direct_erase(ssd,channel,chip,die,plane);
            if (flag_direct_erase==-1)
            {
                flag_gc=interrupt_gc(ssd,channel,chip,die,plane,gc_node);             /*当一个完整的gc操作完成时（已经擦除一个块，回收了一定数量的flash空间），返回1，将channel上相应的gc操作请求节点删除*/
                if (flag_gc==1)
                {
                    delete_gc_node(ssd,channel,gc_node);
                }
            }
            else if (flag_direct_erase==1)
            {
                gc_node->x_end_time = ssd->channel_head[channel].chip_head[chip].next_state_predict_time;
                delete_gc_node(ssd,channel,gc_node);
            }
            return SUCCESS;
        } 
        else
        {
            return FAILURE;
        }		
    }
}



/************************************************************************************************************
 *flag用来标记gc函数是在ssd整个都是idle的情况下被调用的（1），还是确定了channel，chip，die，plane被调用（0）
 *进入gc函数，需要判断是否是不可中断的gc操作，如果是，需要将一整块目标block完全擦除后才算完成；如果是可中断的，
 *在进行GC操作前，需要判断该channel，die是否有子请求在等待操作，如果没有则开始一步一步的操作，找到目标
 *块后，一次执行一个copyback操作，跳出gc函数，待时间向前推进后，再做下一个copyback或者erase操作
 *进入gc函数不一定需要进行gc操作，需要进行一定的判断，当处于硬阈值以下时，必须进行gc操作；当处于软阈值以下时，
 *需要判断，看这个channel上是否有子请求在等待(有写子请求等待就不行，gc的目标die处于busy状态也不行)，如果
 *有就不执行gc，跳出，否则可以执行一步操作
 * =========================================================================================================
 *Flag is used to mark the gc function is called when the ssd is all idle (1), or to determine the channel, 
 *chip, die, plane is called (0) into the gc function, need to determine whether 
 *it is uninterruptible gc Operation, if it is, it needs to completely erase a whole block of target blocks; 
 *if it is interruptible, before the GC operation, it is necessary to judge whether the channel 
 *has a sub-request waiting for operation, if not, start Step by step operation, 
 *after finding the target block, execute a copyback operation at a time, jump out of the gc function, 
 *wait for the time to advance, and then do the next copyback or erase operation into the gc function 
 *does not necessarily need to perform gc operation, need to make certain judgment When it is below 
 *the hard threshold, the gc operation must be performed; when it is below the soft threshold, 
 *it needs to be judged to see if there are sub-requests waiting on this channel 
 *(there is no waiting for the write sub-request, and the target die of gc is in the busy state. 
 *No, if you do not execute gc, jump out, otherwise you can perform one step
 ************************************************************************************************************/
unsigned int gc(struct ssd_info *ssd,unsigned int channel, unsigned int flag)
{
    unsigned int i;
    int flag_direct_erase=1,flag_gc=1,flag_invoke_gc=1;
    unsigned int flag_priority=0;
    struct gc_operation *gc_node=NULL,*gc_p=NULL;

    if (flag==1)                                            /*整个ssd都是IDEL的情况 | The whole ssd is IDLE*/
    {
        for (i=0;i<ssd->parameter->channel_number;i++)
        {
            flag_priority=0;
            flag_direct_erase=1;
            flag_gc=1;
            flag_invoke_gc=1;
            gc_node=NULL;
            gc_p=NULL;
            if((ssd->channel_head[i].current_state==CHANNEL_IDLE)||(ssd->channel_head[i].next_state==CHANNEL_IDLE&&ssd->channel_head[i].next_state_predict_time<=ssd->current_time))
            {
                channel=i;
                if (ssd->channel_head[channel].gc_command!=NULL)
                {
                    gc_for_channel(ssd, channel);
                }
            }
        }
        return SUCCESS;

    } 
    else                                                  /*只需针对某个特定的channel，chip，die进行gc请求的操作(只需对目标die进行判定，看是不是idle） | Only need to perform gc request for a specific channel, chip, die (just judge the target die to see if it is idle)*/
    {
        if ((ssd->parameter->allocation_scheme==1)||((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==1)))
        {
            if ((ssd->channel_head[channel].subs_r_head!=NULL)||(ssd->channel_head[channel].subs_w_head!=NULL))    /*队列上有请求，先服务请求 | There is a request on the queue, serve request first*/
            {
                return 0;
            }
        }

        gc_for_channel(ssd,channel);
        return SUCCESS;
    }
}



/**********************************************************
 *判断是否有子请求血药channel，若果没有返回1就可以发送gc操作
 *如果有返回0，就不能执行gc操作，gc操作被中断
 ***********************************************************/
int decide_gc_invoke(struct ssd_info *ssd, unsigned int channel)      
{
    struct sub_request *sub;
    struct local *location;

    if ((ssd->channel_head[channel].subs_r_head==NULL)&&(ssd->channel_head[channel].subs_w_head==NULL))    /*这里查找读写子请求是否需要占用这个channel，不用的话才能执行GC操作*/
    {
        return 1;                                                                        /*表示当前时间这个channel没有子请求需要占用channel*/
    }
    else
    {
        if (ssd->channel_head[channel].subs_w_head!=NULL)
        {
            return 0;
        }
        else if (ssd->channel_head[channel].subs_r_head!=NULL)
        {
            sub=ssd->channel_head[channel].subs_r_head;
            while (sub!=NULL)
            {
                if (sub->current_state==SR_WAIT)                                         /*这个读请求是处于等待状态，如果他的目标die处于idle，则不能执行gc操作，返回0*/
                {
                    location=find_location(ssd,sub->ppn);
                    if ((ssd->channel_head[location->channel].chip_head[location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[location->channel].chip_head[location->chip].next_state==CHIP_IDLE)&&
                                (ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time<=ssd->current_time)))
                    {
                        free(location);
                        location=NULL;
                        return 0;
                    }
                    free(location);
                    location=NULL;
                }
                else if (sub->next_state==SR_R_DATA_TRANSFER)
                {
                    location=find_location(ssd,sub->ppn);
                    if (ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time<=ssd->current_time)
                    {
                        free(location);
                        location=NULL;
                        return 0;
                    }
                    free(location);
                    location=NULL;
                }
                sub=sub->next_node;
            }
        }
        return 1;
    }
}

