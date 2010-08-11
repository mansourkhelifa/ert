#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <list.h>
#include <hash.h>
#include <fortio.h>
#include <util.h>
#include <ecl_kw.h>
#include <ecl_io_config.h>
#include <ecl_file.h>
#include <list_node.h>
#include <enkf_node.h>
#include <enkf_state.h>
#include <enkf_types.h>
#include <ecl_static_kw.h>
#include <field.h>
#include <field_config.h>
#include <ecl_util.h>
#include <thread_pool.h>
#include <path_fmt.h>
#include <gen_kw.h>
#include <ecl_sum.h>
#include <summary.h>
#include <arg_pack.h>
#include <enkf_fs.h>
#include <basic_driver.h>
#include <node_ctype.h>
#include <job_queue.h>
#include <sched_file.h>
#include <basic_queue_driver.h>
#include <pthread.h>
#include <ext_joblist.h>
#include <stringlist.h>
#include <ensemble_config.h>
#include <model_config.h>
#include <site_config.h>
#include <ecl_config.h>
#include <subst_list.h>
#include <forward_model.h>
#include <log.h>
#include <ecl_endian_flip.h>
#include <ert_template.h>
#include <timer.h>
#include <time_t_vector.h>
#include <member_config.h>
#include <enkf_defaults.h>
#define  ENKF_STATE_TYPE_ID 78132



/**
   This struct is a pure utility structure used to pack the various
   bits and pieces of information needed to start, monitor, and load
   back results from the forward model simulations. 

   Typcially the values in this struct are set from the enkf_main
   object before a forward_step starts.
*/
 
typedef struct run_info_struct {
  bool               	  __ready;              /* An attempt to check the internal state - not active yet. */
  bool               	  active;               /* Is this state object active at all - used for instance in ensemble experiments where only some of the members are integrated. */
  int                	  init_step_parameters; /* The report step we initialize parameters from - will often be equal to step1, but can be different. */
  state_enum         	  init_state_parameter; /* Whether we should init from a forecast or an analyzed state - parameters. */
  state_enum              init_state_dynamic;   /* Whether we should init from a forecast or an analyzed state - dynamic state variables. */
  int                     max_internal_submit;  /* How many times the enkf_state object should try to resubmit when the queueu has said everything is OK - but the load fails. */  
  int                     num_internal_submit;   
  int                     load_start;           /* When loading back results - start at this step. */
  int                	  step1;                /* The forward model is integrated: step1 -> step2 */
  int                	  step2;  	          
  char            	* run_path;             /* The currently used  runpath - is realloced / freed for every step. */
  run_mode_type   	  run_mode;             /* What type of run this is */
  /******************************************************************/
  /* Return value - set by the called routine!!  */
  bool                    runOK;               /* Set to true when the run has completed - AND - the results have been loaded back. */
} run_info_type;
  


/**
   This struct contains various objects which the enkf_state needs
   during operation, which the enkf_state_object *DOES NOT* own. The
   struct only contains pointers to objects owned by (typically) the
   enkf_main object. 

   If the enkf_state object writes to any of the objects in this
   struct that can be considered a serious *BUG*.
*/

typedef struct shared_info_struct {
  const model_config_type     * model_config;      /* .... */
  enkf_fs_type                * fs;                /* The filesystem object - used to load and store nodes. */
  ext_joblist_type            * joblist;           /* The list of external jobs which are installed - and *how* they should be run (with Python code) */
  job_queue_type              * job_queue;         /* The queue handling external jobs. (i.e. LSF / rsh / local / ... )*/ 
  log_type                    * logh;              /* The log handle. */
  ert_templates_type          * templates; 
} shared_info_type;






/*****************************************************************/

struct enkf_state_struct {
  UTIL_TYPE_ID_DECLARATION;
  stringlist_type       * restart_kw_list;
  hash_type    	   	* node_hash;
  subst_list_type       * subst_list;        	   /* This a list of key - value pairs which are used in a search-replace
                                             	      operation on the ECLIPSE data file. Will at least contain the key INIT"
                                             	      - which will describe initialization of ECLIPSE (EQUIL or RESTART).*/
  const ecl_config_type * ecl_config;        	   /* ecl_config object - pure reference to the enkf_main object. */
  ensemble_config_type  * ensemble_config;   	   /* The config nodes for the enkf_node objects contained in node_hash. */
  
  run_info_type         * run_info;          	   /* Various pieces of information needed by the enkf_state object when running the forward model. Updated for each report step.*/
  shared_info_type      * shared_info;       	   /* Pointers to shared objects which is needed by the enkf_state object (read only). */
  member_config_type    * my_config;         	   /* Private config information for this member; not updated during a simulation. */
};

/*****************************************************************/


static void run_info_set_run_path(run_info_type * run_info , int iens , path_fmt_type * run_path_fmt, const subst_list_type * state_subst_list) {
  util_safe_free(run_info->run_path);
  {
    char * tmp = path_fmt_alloc_path(run_path_fmt , false , iens , run_info->step1 , run_info->step2);
    run_info->run_path = subst_list_alloc_filtered_string( state_subst_list , tmp );
    free( tmp );
  }
}



/**
   This function sets the run_info parameters. This is typically called
   (via an enkf_state__ routine) by the external scope handling the forward model.

   When this initialization code has been run we are certain that the
   enkf_state object has all the information it needs to "run itself"
   forward.
*/



static void run_info_summarize( const run_info_type * run_info ) {
  printf("Activ.....................: %d \n",run_info->active);
  printf("Loading parameters from...: %d \n",run_info->init_step_parameters);
  printf("Loading state from........: %d \n",run_info->step1);
  printf("Simulating to.............: %d \n",run_info->step2);
  printf("Loading from step.........: %d \n\n",run_info->load_start);
}


/**
   This function inits the necessary fields in the run_info structure
   to be able to use the xxx_internalize_xxx() functions. Observe that
   trying actually run after the run_info structure has only been
   initialized here will lead to hard failure.

   The inits performed are essential for running, not only for the
   internalizing.
*/


static void run_info_init_for_load(run_info_type * run_info , 
                                   int load_start, 
                                   int step1,
                                   int step2,
                                   int iens,
                                   path_fmt_type * run_path_fmt ,
                                   const subst_list_type * state_subst_list) {
  run_info->step1      = step1;
  run_info->step2      = step2;
  run_info->load_start = load_start;
  run_info_set_run_path(run_info , iens , run_path_fmt , state_subst_list );
}


static void run_info_set(run_info_type * run_info        , 
			 run_mode_type run_mode          , 
			 bool active                     , 
                         int max_internal_submit         ,
			 int init_step_parameters        ,      
			 state_enum init_state_parameter ,
                         state_enum init_state_dynamic   ,
                         int load_start                  , 
			 int step1                       , 
			 int step2                       ,      
			 int iens                             , 
			 path_fmt_type * run_path_fmt ,
                         const subst_list_type * state_subst_list) {

  run_info->active               = active;
  run_info->init_step_parameters = init_step_parameters;
  run_info->init_state_parameter = init_state_parameter;
  run_info->init_state_dynamic = init_state_dynamic;
  run_info->runOK                = false;
  run_info->__ready              = true;
  run_info->run_mode             = run_mode;
  run_info->max_internal_submit  = max_internal_submit;
  run_info->num_internal_submit  = 0;
  run_info_init_for_load( run_info , load_start , step1 , step2 , iens , run_path_fmt , state_subst_list);
}


static run_info_type * run_info_alloc() {
  run_info_type * run_info = util_malloc(sizeof * run_info , __func__);
  run_info->run_path = NULL;
  return run_info;
}


static void run_info_free(run_info_type * run_info) {
  util_safe_free(run_info->run_path);
  free(run_info);
}


static void run_info_complete_run(run_info_type * run_info) {
  if (run_info->runOK) {
    util_safe_free(run_info->run_path);
    run_info->run_path = NULL;
  }
}



/*****************************************************************/

static shared_info_type * shared_info_alloc(const site_config_type * site_config , const model_config_type * model_config, enkf_fs_type * fs , log_type * logh , ert_templates_type * templates) {
  shared_info_type * shared_info = util_malloc(sizeof * shared_info , __func__);

  shared_info->fs           = fs;
  shared_info->joblist      = site_config_get_installed_jobs( site_config );
  shared_info->job_queue    = site_config_get_job_queue( site_config );
  shared_info->model_config = model_config;
  shared_info->logh         = logh;
  shared_info->templates    = templates;
  return shared_info;
}


static void shared_info_free(shared_info_type * shared_info) {
  /** 
      Adding something here is a BUG - this object does 
      not own anything.
  */
  free( shared_info );
}

                                         



/*****************************************************************/
/** Helper classes complete - starting on the enkf_state proper object. */
/*****************************************************************/



void enkf_state_initialize(enkf_state_type * enkf_state , const stringlist_type * param_list) {
  int ip;
  for (ip = 0; ip < stringlist_get_size(param_list); ip++) {
    int iens = enkf_state_get_iens( enkf_state );
    enkf_node_type * param_node = enkf_state_get_node( enkf_state , stringlist_iget( param_list , ip));
    if (enkf_node_initialize( param_node , iens))
      enkf_fs_fwrite_node(enkf_state_get_fs_ref( enkf_state ) , param_node , 0 , iens , ANALYZED);
  }
}







/**
   The run_info->run_path variable is in general NULL. It is given a
   valid value before a simulation starts, holds on to that value
   through the simulation, and is then freed (and set to NULL) when
   the simulation ends.

   The *ONLY* point when an external call to this function should give
   anything is when the forward model has failed, then
   run_info_complete_run() has left the run_path intact.
*/

const char * enkf_state_get_run_path(const enkf_state_type * enkf_state) { 
  return enkf_state->run_info->run_path; 
}




/*
  void enkf_state_set_iens(enkf_state_type * enkf_state , int iens) {
  enkf_state->my_iens = iens;
  }
*/

int  enkf_state_get_iens(const enkf_state_type * enkf_state) {
  return member_config_get_iens( enkf_state->my_config );
}

member_config_type * enkf_state_get_member_config(const enkf_state_type * enkf_state) {
  return enkf_state->my_config;
}


enkf_fs_type * enkf_state_get_fs_ref(const enkf_state_type * state) {
  return state->shared_info->fs;
}


static UTIL_SAFE_CAST_FUNCTION( enkf_state , ENKF_STATE_TYPE_ID )



static void enkf_state_add_subst_kw(enkf_state_type * enkf_state , const char * kw , const char * value , const char * doc_string) {
  char * tagged_key = util_alloc_sprintf( INTERNAL_DATA_KW_TAG_FORMAT , kw );
  subst_list_append_owned_ref(enkf_state->subst_list , tagged_key , util_alloc_string_copy(value) , doc_string);
  free(tagged_key);
}





/**
   This function must be called each time the eclbase_fmt has been
   updated.
*/

void enkf_state_update_eclbase( enkf_state_type * enkf_state ) {
  const char * eclbase  = member_config_update_eclbase( enkf_state->my_config , enkf_state->ecl_config , enkf_state->subst_list);
  const char * casename = member_config_get_casename( enkf_state->my_config ); /* Mostly NULL */
  {
    enkf_state_add_subst_kw(enkf_state , "ECL_BASE"    , eclbase , NULL);  
    enkf_state_add_subst_kw(enkf_state , "ECLBASE"     , eclbase , NULL);  
    
    if (casename == NULL)
      enkf_state_add_subst_kw( enkf_state , "CASE" , eclbase , NULL);      /* No CASE_TABLE loaded - using the eclbase as default. */
    else
      enkf_state_add_subst_kw( enkf_state , "CASE" , casename , NULL);
    
  }
}



/**
   Sets all the static subst keywords which will not change during the simulation.
*/
static void enkf_state_set_static_subst_kw(enkf_state_type * enkf_state) {

  {
    int    iens        = member_config_get_iens( enkf_state->my_config );
    char * iens_s      = util_alloc_sprintf("%d"   , iens);
    char * iens4_s     = util_alloc_sprintf("%04d" , iens);

    enkf_state_add_subst_kw(enkf_state , "IENS"        , iens_s      , NULL);
    enkf_state_add_subst_kw(enkf_state , "IENS4"       , iens4_s     , NULL);
    
    free(iens_s);
    free(iens4_s);
  }
  enkf_state_update_eclbase( enkf_state );
}

/**
   Two small callback functions used to return (string representation)
   of random integer and random float. The memorty allocated by these
   functions will be freed by the calling scope.
*/

static char * enkf_state_subst_randint(const char * key , void * arg) {
  return util_alloc_sprintf("%d" , rand());
}

static char * enkf_state_subst_randfloat(const char * key , void * arg) {
  return util_alloc_sprintf("%12.10f" , 1.0 * rand() / RAND_MAX);
}


static void enkf_state_add_nodes( enkf_state_type * enkf_state, const ensemble_config_type * ensemble_config) {
  stringlist_type * keylist  = ensemble_config_alloc_keylist(ensemble_config);
  int keys        = stringlist_get_size(keylist);

  for (int ik = 0; ik < keys; ik++) {
    const char * key = stringlist_iget(keylist, ik);
    const enkf_config_node_type * config_node = ensemble_config_get_node(ensemble_config , key);
    enkf_state_add_node(enkf_state , key , config_node);
  }

  stringlist_free(keylist);
}


/**
   This variable is on a per-instance basis, but that is not really
   supported. The exported functionality applies to all realizations.
*/

bool enkf_state_get_pre_clear_runpath( const enkf_state_type * enkf_state ) {
  return member_config_pre_clear_runpath( enkf_state->my_config );
}


void enkf_state_set_pre_clear_runpath( enkf_state_type * enkf_state , bool pre_clear_runpath ) {
  member_config_set_pre_clear_runpath( enkf_state->my_config , pre_clear_runpath );
}



enkf_state_type * enkf_state_alloc(int iens,
                                   enkf_fs_type              * fs, 
                                   const char                * casename , 
                                   bool                        pre_clear_runpath , 
				   keep_runpath_type           keep_runpath , 
				   const model_config_type   * model_config,
				   ensemble_config_type      * ensemble_config,
				   const site_config_type    * site_config,
				   const ecl_config_type     * ecl_config,
                                   log_type                  * logh,
                                   ert_templates_type        * templates,
                                   subst_list_type           * subst_parent) { 
  
  enkf_state_type * enkf_state  = util_malloc(sizeof *enkf_state , __func__);
  UTIL_TYPE_ID_INIT( enkf_state , ENKF_STATE_TYPE_ID );

  enkf_state->ensemble_config   = ensemble_config;
  enkf_state->ecl_config        = ecl_config;
  enkf_state->shared_info       = shared_info_alloc(site_config , model_config , fs , logh, templates);
  enkf_state->run_info          = run_info_alloc();
  
  enkf_state->node_hash         = hash_alloc();
  enkf_state->restart_kw_list   = stringlist_alloc_new();
  enkf_state->subst_list        = subst_list_alloc( subst_parent );

  
  /*
    The user MUST specify an INIT_FILE, and for the first timestep the
   <INIT> tag in the data file will be replaced by an 

INCLDUE
   EQUIL_INIT_FILE

   statement. When considering the possibility of estimating EQUIL this
   require a real understanding of the treatment of paths:

   * If not estimating the EQUIL contacts, all members should use the
     same init_file. To ensure this the user must specify the ABSOLUTE
     PATH to a file containing initialization information.

   * If the user is estimating initial contacts, the INIT_FILE must
     point to the ecl_file of the EQUIL keyword, this must be a pure
     filename without any path component (as it will be generated by
     the EnKF program, and placed in the run_path directory). We could
     let the EnKF program use the ecl_file of the EQUIL keyword if it
     is present.

   The <INIT> key is actually initialized in the
   enkf_state_set_dynamic_subst_kw() function.
  */

  /**
     Adding all the subst_kw keywords here, with description. Listing
     all of them here in one go guarantees that we have control over
     the ordering (which is interesting because the substititions are
     done in a cascade like fashion). The user defined keywords are
     added first, so that these can refer to the built in keywords.
  */
  
  enkf_state_add_subst_kw(enkf_state , "RUNPATH"       , "---" , "The absolute path of the current forward model instance. ");
  enkf_state_add_subst_kw(enkf_state , "IENS"          , "---" , "The realisation number for this realization.");
  enkf_state_add_subst_kw(enkf_state , "IENS4"         , "---" , "The realization number for this realization - formated with %04d.");
  enkf_state_add_subst_kw(enkf_state , "ECLBASE"       , "---" , "The ECLIPSE basename for this realization.");
  enkf_state_add_subst_kw(enkf_state , "ECL_BASE"      , "---" , "Depreceated - use ECLBASE instead.");
  enkf_state_add_subst_kw(enkf_state , "SMSPEC"        , "---" , "The ECLIPSE SMSPEC file for this realization.");
  enkf_state_add_subst_kw(enkf_state , "TSTEP1"        , "---" , "The initial report step for this simulation.");
  enkf_state_add_subst_kw(enkf_state , "TSTEP2"        , "---" , "The final report step for this simulation.");
  enkf_state_add_subst_kw(enkf_state , "TSTEP1_04"     , "---" , "The initial report step for this simulation - formated with %04d.");
  enkf_state_add_subst_kw(enkf_state , "TSTEP2_04"     , "---" , "The final report step for this simulation - formated withh %04d.");
  enkf_state_add_subst_kw(enkf_state , "RESTART_FILE1" , "---" , "The ECLIPSE restart file this simulation starts with.");
  enkf_state_add_subst_kw(enkf_state , "RESTART_FILE2" , "---" , "The ECLIPSE restart file this simulation should end with.");
  enkf_state_add_subst_kw(enkf_state , "RANDINT"       , "---" , "Random integer value (depreceated: use __RANDINT__() instead).");
  enkf_state_add_subst_kw(enkf_state , "RANDFLOAT"     , "---" , "Random float value (depreceated: use __RANDFLOAT__() instead).");
  enkf_state_add_subst_kw(enkf_state , "INIT"          , "---" , "The code which will be inserted at the <INIT> tag"); 
  if (casename != NULL) 
    enkf_state_add_subst_kw(enkf_state , "CASE" , casename , "The casename for this realization - as loaded from the CASE_TABLE file.");
  else
    enkf_state_add_subst_kw(enkf_state , "CASE" , "---" , "The casename for this realization - similar to ECLBASE.");
  
  enkf_state->my_config = member_config_alloc( iens , casename , pre_clear_runpath , keep_runpath , ecl_config , ensemble_config , fs);
  enkf_state_set_static_subst_kw( enkf_state );

  enkf_state_add_nodes( enkf_state , ensemble_config );

  return enkf_state;
}



enkf_state_type * enkf_state_copyc(const enkf_state_type * src) {
  util_abort("%s: not implemented \n",__func__);
  return NULL;
}



static bool enkf_state_has_node(const enkf_state_type * enkf_state , const char * node_key) {
  bool has_node = hash_has_key(enkf_state->node_hash , node_key);
  return has_node;
}



/**
   The enkf_state inserts a reference to the node object. The
   enkf_state object takes ownership of the node object, i.e. it will
   free it when the game is over.

   Observe that if the node already exists the existing node will be
   removed (freed and so on ... ) from the enkf_state object before
   adding the new; this was previously considered a run-time error.
*/


void enkf_state_add_node(enkf_state_type * enkf_state , const char * node_key , const enkf_config_node_type * config) {
  if (enkf_state_has_node(enkf_state , node_key)) 
    enkf_state_del_node( enkf_state , node_key );   /* Deleting the old instance (if we had one). */
  {
    enkf_node_type *enkf_node = enkf_node_alloc(config);
    hash_insert_hash_owned_ref(enkf_state->node_hash , node_key , enkf_node, enkf_node_free__);
  
    /* Setting the global subst list so that the GEN_KW templates can contain e.g. <IENS> and <CWD>. */
    if (enkf_node_get_impl_type( enkf_node ) == GEN_KW)
      gen_kw_set_subst_parent( enkf_node_value_ptr( enkf_node ) , enkf_state->subst_list );
  }
}


void enkf_state_update_node( enkf_state_type * enkf_state , const char * node_key ) {
  const enkf_config_node_type * config_node = ensemble_config_get_node( enkf_state->ensemble_config , node_key );
  if (!enkf_state_has_node( enkf_state , node_key))
    enkf_state_add_node( enkf_state , node_key , config_node );  /* Add a new node */
  else {
    bool modified = true;   /* ehhhh? */

    if (modified)
      enkf_state_add_node( enkf_state , node_key , config_node );  
  }
}



static void enkf_state_internalize_dynamic_results(enkf_state_type * enkf_state , const model_config_type * model_config , bool * loadOK) {
  /* IFF reservoir_simulator == ECLIPSE ... */
  if (true) {
    const run_info_type   * run_info       = enkf_state->run_info;
    int        load_start                  = run_info->load_start;
    int        report_step;

    if (load_start == 0)  /* Do not attempt to load the "S0000" summary results. */
      load_start++;

    {
      member_config_type * my_config         = enkf_state->my_config;
      const char            * eclbase        = member_config_get_eclbase( my_config );
      const ecl_config_type * ecl_config     = enkf_state->ecl_config;
      const bool fmt_file    		     = ecl_config_get_formatted(ecl_config);
      ecl_sum_type          * summary        = NULL;
      
      
      /* Looking for summary files on disk, and loading them. */
      {
        stringlist_type * data_files = stringlist_alloc_new();
        char * header_file           = ecl_util_alloc_exfilename(run_info->run_path , eclbase , ECL_SUMMARY_HEADER_FILE , fmt_file , -1);
        char * unified_file          = ecl_util_alloc_exfilename(run_info->run_path , eclbase , ECL_UNIFIED_SUMMARY_FILE , fmt_file ,  -1);
      
        /* Should we load from a unified summary file, or from several non-unified files? */
        if (unified_file != NULL) 
          /* Use unified file: */
          stringlist_append_owned_ref( data_files , unified_file);
        else {
          /* Use several non unified files. */
          
          /* Bypassing the query to model_config_load_results() */
          report_step = load_start;
          while (true) {
            char * summary_file = ecl_util_alloc_exfilename(run_info->run_path , eclbase , ECL_SUMMARY_FILE , fmt_file ,  report_step);
            if (summary_file == NULL) {
              /* If in prediction mode, we jump ship when we reach the first non-existing file. */
              if (run_info->run_mode == ENSEMBLE_PREDICTION)
                break;
            } else
              stringlist_append_owned_ref( data_files , summary_file);
            
            report_step++;
            if (run_info->run_mode != ENSEMBLE_PREDICTION && report_step > run_info->step2)
              break;
          }
        }  


        if ((header_file != NULL) && (stringlist_get_size(data_files) > 0)) 
          summary = ecl_sum_fread_alloc(header_file , data_files , SUMMARY_KEY_JOIN_STRING );
        
        stringlist_free( data_files );
        util_safe_free( header_file );

        /** OK - now we have actually loaded the ecl_sum instance, or ecl_sum == NULL. */
        if (summary != NULL) {
          /* The actual loading internalizing - from ecl_sum -> enkf_node. */
          const shared_info_type   * shared_info = enkf_state->shared_info;
          const int iens                         = member_config_get_iens( my_config );
          const int step2                        = ecl_sum_get_last_report_step( summary );  /* Step2 is just taken from the number of steps found in the summary file. */
          
          for (report_step = load_start; report_step <= step2; report_step++) {
            hash_iter_type * iter       = hash_iter_alloc(enkf_state->node_hash);
            while ( !hash_iter_is_complete(iter) ) {
              enkf_node_type * node = hash_iter_get_next_value(iter);
              if (enkf_node_get_var_type(node) == DYNAMIC_RESULT) {
                /* We internalize all DYNAMIC_RESULT nodes without any further ado. */
                
                if (enkf_node_ecl_load(node , run_info->run_path , summary , NULL , report_step , iens))   /* Loading/internalizing */
                  enkf_fs_fwrite_node(shared_info->fs , node , report_step , iens , FORECAST);             /* Saving to disk */
                else {
                  *loadOK = false;
                  log_add_fmt_message(shared_info->logh , 3 , NULL , "[%03d:%04d] Failed to load data for node:%s.",iens , report_step , enkf_node_get_key( node ));
                } 
                
              }
            } 
            hash_iter_free(iter);
            member_config_iset_sim_time( my_config , report_step , ecl_sum_get_report_time( summary , report_step ));
          }
          
          ecl_sum_free( summary ); 
          member_config_fwrite_sim_time( my_config , shared_info->fs );
        } 
      }
    }
  }
}



/**
   The ECLIPSE restart files can contain several instances of the same
   keyword, e.g. AQUIFER info can come several times with identical
   headers, also when LGR is in use the same header for
   e.g. PRESSURE/SWAT/INTEHEAD/... will occur several times. The
   enkf_state/ensembl_config objects require unique keys.

   This function takes keyword string and an occurence number, and
   combine them to one new string like this:

     __realloc_static_kw("INTEHEAD" , 0) ==>  "INTEHEAD_0"

   In the enkf layer the key used will then be INTEHEAD_0. 
*/


static char * __realloc_static_kw(char * kw , int occurence) {
  char * new_kw = util_alloc_sprintf("%s_%d" , kw , occurence);
  free(kw);
  ecl_util_escape_kw(new_kw);  
  return new_kw;
}



/**
   This function loads the STATE from a forward simulation. In ECLIPSE
   speak that means to load the solution vectors (PRESSURE/SWAT/..)
   and the necessary static keywords.
   
   When the state has been loaded it goes straight to disk.
*/

static void enkf_state_internalize_state(enkf_state_type * enkf_state , const model_config_type * model_config , int report_step , bool * loadOK) {
  member_config_type * my_config   = enkf_state->my_config;
  shared_info_type   * shared_info = enkf_state->shared_info;
  run_info_type      * run_info    = enkf_state->run_info;
  const int  iens                  = member_config_get_iens( my_config ); 
  const bool fmt_file              = ecl_config_get_formatted(enkf_state->ecl_config);
  const bool unified               = ecl_config_get_unified_restart(enkf_state->ecl_config);
  const bool internalize_state     = model_config_internalize_state( model_config , report_step );
  ecl_file_type  * restart_file;
  
  
  /**
     Loading the restart block.
  */
  
  if (unified) 
    util_abort("%s: sorry - unified restart files are not supported \n",__func__);
  {
    char * file  = ecl_util_alloc_exfilename(run_info->run_path , member_config_get_eclbase(enkf_state->my_config) , ECL_RESTART_FILE , fmt_file , report_step);
    if (file != NULL) {
      restart_file = ecl_file_fread_alloc(file );
      free(file);
    } else 
      restart_file = NULL;  /* No restart information was found; if that is expected the program will fail hard in the enkf_node_ecl_load() functions. */
  }
  
  /*****************************************************************/
  
  
  /**
     Iterating through the restart file:
     
     1. Build up enkf_state->restart_kw_list.
     2. Send static keywords straight out.
  */
  
  if (restart_file != NULL) {
    stringlist_clear( enkf_state->restart_kw_list );
    {
      int ikw; 
      for (ikw =0; ikw < ecl_file_get_num_kw( restart_file ); ikw++) {
	enkf_impl_type impl_type;
	const ecl_kw_type * ecl_kw = ecl_file_iget_kw( restart_file , ikw);
	int occurence              = ecl_file_iget_occurence( restart_file , ikw ); /* This is essentially the static counter value. */
	char * kw                  = ecl_kw_alloc_strip_header( ecl_kw );
	/** 
	    Observe that this test will never succeed for static keywords,
	    because the internalized key has appended a _<occurence>.
	*/
	if (ensemble_config_has_key(enkf_state->ensemble_config , kw)) {
	  /**
	     This is poor-mans treatment of LGR. When LGR is used the restart file
	     will contain repeated occurences of solution vectors, like
	     PRESSURE. The first occurence of PRESSURE will be for the ordinary
	     grid, and then there will be subsequent PRESSURE sections for each
	     LGR section. The way this is implemented here is as follows:
	     
	     1. The first occurence of pressure is internalized as the enkf_node
	        pressure (if we indeed have a pressure node).
	     
	     2. The consecutive pressure nodes are internalized as static
	        parameters.
	   
	     The variable 'occurence' is the key here.
	  */
	  
	  if (occurence == 0) {
	    const enkf_config_node_type * config_node = ensemble_config_get_node(enkf_state->ensemble_config , kw);
	    impl_type = enkf_config_node_get_impl_type(config_node);
	  } else 
	    impl_type = STATIC;
	} else
	  impl_type = STATIC;
	
	
	if (impl_type == FIELD) 
	  stringlist_append_copy(enkf_state->restart_kw_list , kw);
	else if (impl_type == STATIC) {
	  if (ecl_config_include_static_kw(enkf_state->ecl_config , kw)) {
	    /* It is a static kw like INTEHEAD or SCON */
	    /* 
	       Observe that for static keywords we do NOT ask the node 'privately' if
	       internalize_state is false: It is impossible to single out static keywords for
	       internalization.
	    */
	    
	    /* Now we mangle the static keyword .... */
	    kw = __realloc_static_kw(kw , occurence);
	    
	    if (internalize_state) {  
	      stringlist_append_copy( enkf_state->restart_kw_list , kw);
	      
              ensemble_config_ensure_static_key(enkf_state->ensemble_config , kw );
              
	      if (!enkf_state_has_node(enkf_state , kw)) {
		const enkf_config_node_type * config_node = ensemble_config_get_node(enkf_state->ensemble_config , kw);
		enkf_state_add_node(enkf_state , kw , config_node); 
	      }
	      
	      /* 
		 The following thing can happen:
		 
		 1. A static keyword appears at report step n, and is added to the enkf_state
		    object.
		 
		 2. At report step n+k that static keyword is no longer active, and it is
		    consequently no longer part of restart_kw_list().
		 
		 3. However it is still part of the enkf_state. Not loaded here, and subsequently
	            purged from enkf_main.
	       
		 One keyword where this occurs is FIPOIL, which at least might appear only in the
		 first restart file. Unused static keywords of this type are purged from the
		 enkf_main object by a call to enkf_main_del_unused_static(). The purge is based on
		 looking at the internal __report_step state of the static kw.
	      */
	      
	      {
		enkf_node_type * enkf_node         = enkf_state_get_node(enkf_state , kw);
		enkf_node_ecl_load_static(enkf_node , ecl_kw , report_step , iens);
		/*
		  Static kewyords go straight out ....
		*/
		enkf_fs_fwrite_node(shared_info->fs , enkf_node , report_step , iens , FORECAST);
		enkf_node_free_data(enkf_node);
	      }
	    }
	  } 
	} else
	  util_abort("%s: hm - something wrong - can (currently) only load FIELD/STATIC implementations from restart files - aborting \n",__func__);
	free(kw);
      }
      enkf_fs_fwrite_restart_kw_list( shared_info->fs , report_step , iens , enkf_state->restart_kw_list );
    }
  }
  
  /******************************************************************/
  /** 
      Starting on the enkf_node_ecl_load() function calls. This is where the
      actual loading (apart from static keywords) is done. Observe that this
      loading might involve other load functions than the ones used for
      loading PRESSURE++ from ECLIPSE restart files (e.g. for loading seismic
      results..)
  */
  
  {
    hash_iter_type * iter = hash_iter_alloc(enkf_state->node_hash);
    while ( !hash_iter_is_complete(iter) ) {
      enkf_node_type * enkf_node = hash_iter_get_next_value(iter);
      if (enkf_node_get_var_type(enkf_node) == DYNAMIC_STATE) {
	bool internalize_kw = internalize_state;
	if (!internalize_kw)
	  internalize_kw = enkf_node_internalize(enkf_node , report_step);

	if (internalize_kw) {
	  if (enkf_node_has_func(enkf_node , ecl_load_func)) {
	    if (enkf_node_ecl_load(enkf_node , run_info->run_path , NULL , restart_file , report_step , iens ))
              enkf_fs_fwrite_node(shared_info->fs , enkf_node , report_step , iens , FORECAST);
            else {
              *loadOK = false;
              log_add_fmt_message(shared_info->logh , 3 , NULL , "[%03d:%04d] Failed load data for node:%s.",iens , report_step , enkf_node_get_key( enkf_node ));
            }

	  }
	} 
      } 
    }                                                                      
    hash_iter_free(iter);
  }
  
  /*****************************************************************/
  /* Cleaning up */
  if (restart_file != NULL) ecl_file_free( restart_file );
}






/**
   This function loads the results from a forward simulations from report_step1
   to report_step2. The details of what to load are in model_config and the
   spesific nodes for special cases.

   Will mainly be called at the end of the forward model, but can also
   be called manually from external scope.
*/
   

static void enkf_state_internalize_results(enkf_state_type * enkf_state , bool * loadOK) {
  run_info_type             * run_info   = enkf_state->run_info;
  const model_config_type * model_config = enkf_state->shared_info->model_config;
  int report_step;

  for (report_step = run_info->load_start; report_step <= run_info->step2; report_step++) {
    if (model_config_load_state( model_config , report_step)) 
      enkf_state_internalize_state(enkf_state , model_config , report_step , loadOK);
  }
  
  enkf_state_internalize_dynamic_results(enkf_state , model_config , loadOK);
}


void * enkf_state_internalize_results_mt( void * arg ) {
  arg_pack_type * arg_pack = arg_pack_safe_cast( arg );
  enkf_state_type * enkf_state = arg_pack_iget_ptr( arg_pack , 0 );
  int load_start               = arg_pack_iget_int( arg_pack , 1 );
  int step1                    = arg_pack_iget_int( arg_pack , 2 );
  int step2                    = arg_pack_iget_int( arg_pack , 3 );
  bool loadOK                  = true;

  
  run_info_init_for_load( enkf_state->run_info , 
                          load_start , 
                          step1 , 
                          step2 , 
                          member_config_get_iens( enkf_state->my_config ) , 
                          model_config_get_runpath_fmt( enkf_state->shared_info->model_config ) , 
                          enkf_state->subst_list );
  
  enkf_state_internalize_results( enkf_state , &loadOK);
  
  return NULL;
}  




/**
   Observe that this function uses run_info->step1 to load all the nodes which
   are needed in the restart file. I.e. if you have carefully prepared a funny
   state with dynamic/static data which do not agree with the current value of
   run_info->step1 YOUR STATE WILL BE OVERWRITTEN.
*/

static void enkf_state_write_restart_file(enkf_state_type * enkf_state) {
  shared_info_type * shared_info       = enkf_state->shared_info;
  const member_config_type * my_config = enkf_state->my_config;
  const run_info_type      * run_info  = enkf_state->run_info;
  const bool fmt_file  		       = ecl_config_get_formatted(enkf_state->ecl_config);
  const int iens                       = member_config_get_iens( my_config );
  char * restart_file    	       = ecl_util_alloc_filename(run_info->run_path , member_config_get_eclbase( enkf_state->my_config ) , ECL_RESTART_FILE , fmt_file , run_info->step1);
  fortio_type * fortio   	       = fortio_fopen(restart_file , "w" , ECL_ENDIAN_FLIP , fmt_file);
  const char * kw;
  int          ikw;

  if (stringlist_get_size(enkf_state->restart_kw_list) == 0)
    enkf_fs_fread_restart_kw_list(shared_info->fs , run_info->step1 , iens , enkf_state->restart_kw_list);

  for (ikw = 0; ikw < stringlist_get_size(enkf_state->restart_kw_list); ikw++) {
    kw = stringlist_iget( enkf_state->restart_kw_list , ikw);
    /* 
       Observe that here we are *ONLY* iterating over the
       restart_kw_list instance, and *NOT* the enkf_state
       instance. I.e. arbitrary dynamic keys, and no-longer-active
       static kewyords should not show up.

       If the restart kw_list asks for a keyword which we do not have,
       we assume it is a static keyword and add it it to the
       enkf_state instance. 
       
       This is a bit unfortunate, because a bug/problem of some sort,
       might be masked (seemingly solved) by adding a static keyword,
       before things blow up completely at a later instant.
    */  
    if (!ensemble_config_has_key(enkf_state->ensemble_config , kw)) 
      ensemble_config_add_node(enkf_state->ensemble_config , kw , STATIC_STATE , STATIC , NULL , NULL , NULL );
    
    if (!enkf_state_has_node(enkf_state , kw)) {
      const enkf_config_node_type * config_node = ensemble_config_get_node(enkf_state->ensemble_config , kw);
      enkf_state_add_node(enkf_state , kw , config_node); 
    }
    
    {
      enkf_node_type * enkf_node = enkf_state_get_node(enkf_state , kw); 
      enkf_var_type var_type = enkf_node_get_var_type(enkf_node); 
      if (var_type == STATIC_STATE) 
	enkf_fs_fread_node(shared_info->fs , enkf_node , run_info->step1 , iens , run_info->init_state_dynamic);
      
      if (var_type == DYNAMIC_STATE) {
	/* Pressure and saturations */
	if (enkf_node_get_impl_type(enkf_node) == FIELD)
	  enkf_node_ecl_write(enkf_node , NULL , fortio , run_info->step1);
	else 
	  util_abort("%s: internal error wrong implementetion type:%d - node:%s aborting \n",__func__ , enkf_node_get_impl_type(enkf_node) , enkf_node_get_key(enkf_node));
      } else if (var_type == STATIC_STATE) {
	enkf_node_ecl_write(enkf_node , NULL , fortio , run_info->step1);
	enkf_node_free_data(enkf_node); /* Just immediately discard the static data. */
      } else {
	fprintf(stderr,"var_type: %d \n",var_type);
	fprintf(stderr,"node    : %s \n",enkf_node_get_key(enkf_node));
	util_abort("%s: internal error - should not be here ... \n",__func__);
      }
      
    }
  }
  fortio_fclose(fortio);
  free(restart_file);
}



/**
  This function writes out all the files needed by an ECLIPSE simulation, this
  includes the restart file, and the various INCLUDE files corresponding to
  parameteres estimated by EnKF.

  The writing of restart file is delegated to enkf_state_write_restart_file().
*/

void enkf_state_ecl_write(enkf_state_type * enkf_state) {
  const run_info_type * run_info         = enkf_state->run_info;
  
  if (run_info->step1 > 0)
    enkf_state_write_restart_file(enkf_state);
  else {
    /*
      These keywords are added here becasue otherwise the main loop
      below will try to write them with ecl_write - and that will fail
      (for report_step 0).
    */
    stringlist_append_copy(enkf_state->restart_kw_list , "SWAT");
    stringlist_append_copy(enkf_state->restart_kw_list , "SGAS");
    stringlist_append_copy(enkf_state->restart_kw_list , "PRESSURE");
    stringlist_append_copy(enkf_state->restart_kw_list , "RV");
    stringlist_append_copy(enkf_state->restart_kw_list , "RS");
  }
  
  {
    /** 
        This iteration manipulates the hash (thorugh the enkf_state_del_node() call) 
        
        -----------------------------------------------------------------------------------------
        T H I S  W I L L  D E A D L O C K  I F  T H E   H A S H _ I T E R  A P I   I S   U S E D.
        -----------------------------------------------------------------------------------------
    */
    
    const int num_keys = hash_get_size(enkf_state->node_hash);
    char ** key_list   = hash_alloc_keylist(enkf_state->node_hash);
    int ikey;
    
    for (ikey = 0; ikey < num_keys; ikey++) {
      if (!stringlist_contains(enkf_state->restart_kw_list , key_list[ikey])) {          /* Make sure that the elements in the restart file are not written (again). */
        enkf_node_type * enkf_node = hash_get(enkf_state->node_hash , key_list[ikey]);
        if (enkf_node_get_var_type( enkf_node ) != STATIC_STATE)                          /* Ensure that no-longer-active static keywords do not create problems. */
          enkf_node_ecl_write(enkf_node , run_info->run_path , NULL , run_info->step1); 
      }
    }
    util_free_stringlist(key_list , num_keys);
  }
}

/**
   This function takes a report_step and a analyzed|forecast state as
  input; the enkf_state instance is set accordingly and written to
  disk.
*/


void enkf_state_fwrite(const enkf_state_type * enkf_state , int mask , int report_step , state_enum state) {
  shared_info_type * shared_info = enkf_state->shared_info;
  const member_config_type * my_config = enkf_state->my_config;
  const int num_keys = hash_get_size(enkf_state->node_hash);
  char ** key_list   = hash_alloc_keylist(enkf_state->node_hash);
  int ikey;
  
  for (ikey = 0; ikey < num_keys; ikey++) {
    enkf_node_type * enkf_node = hash_get(enkf_state->node_hash , key_list[ikey]);
    if (enkf_node_include_type(enkf_node , mask))                       
      enkf_fs_fwrite_node(shared_info->fs , enkf_node , report_step , member_config_get_iens( my_config ) , state);
  }                                                                     
  util_free_stringlist(key_list , num_keys);
}


void enkf_state_fread(enkf_state_type * enkf_state , int mask , int report_step , state_enum state) {
  shared_info_type * shared_info = enkf_state->shared_info;
  const member_config_type * my_config = enkf_state->my_config;
  const int num_keys = hash_get_size(enkf_state->node_hash);
  char ** key_list   = hash_alloc_keylist(enkf_state->node_hash);
  int ikey;
  
  for (ikey = 0; ikey < num_keys; ikey++) {
    enkf_node_type * enkf_node = hash_get(enkf_state->node_hash , key_list[ikey]);
    if (enkf_node_include_type(enkf_node , mask)) 
      enkf_fs_fread_node(shared_info->fs , enkf_node , report_step , member_config_get_iens( my_config ) , state);
  }
  util_free_stringlist(key_list , num_keys);
}


/**
   This function will load all the nodes listed in the current
   restart_kw_list; in addition to all other variable of type
   DYNAMIC_STATE. Observe that for DYNAMIC state nodes it will try
   firt analyzed state and then forecast state.
*/


static void enkf_state_fread_state_nodes(enkf_state_type * enkf_state , int report_step , state_enum load_state) {
  shared_info_type * shared_info       = enkf_state->shared_info;
  const member_config_type * my_config = enkf_state->my_config;
  const int iens                       = member_config_get_iens( my_config );
  int ikey;


  /* 
     First pass - load all the STATIC nodes. It is essential to use
     the restart_kw_list when loading static nodes, otherwise static
     nodes which were only present at e.g. step == 0 will create
     problems: (They are in the enkf_state hash table because they
     were seen at step == 0, but have not been seen subesquently and
     the loading fails.)
  */

  enkf_fs_fread_restart_kw_list(shared_info->fs , report_step , iens , enkf_state->restart_kw_list);
  for (ikey = 0; ikey < stringlist_get_size( enkf_state->restart_kw_list) ; ikey++) {
    const char * key = stringlist_iget( enkf_state->restart_kw_list, ikey);
    enkf_node_type * enkf_node;
    enkf_var_type    var_type;

    /*
      The restart_kw_list mentions a keyword which is (not yet) part
      of the enkf_state object. This is assumed to be a static keyword
      and added as such.
      
      This will break hard for the following situation:

        1. Someone has simulated with a dynamic keyword (i.e. field
           FIELD1).

        2. the fellow decides to remove field1 from the configuraton
           and restart a simulation.

      In this case the code will find FIELD1 in the restart_kw_list,
      it will then be automatically added as a static keyword; and
      then final fread_node() function will fail with a type mismatch
      (or node not found); hopefully this scenario is not very
      probable.
    */
    
    /* add the config node. */
    if (!ensemble_config_has_key( enkf_state->ensemble_config , key))
      ensemble_config_ensure_static_key( enkf_state->ensemble_config , key);

    /* Add the state node */
    if (!enkf_state_has_node( enkf_state , key )) {
      const enkf_config_node_type * config_node = ensemble_config_get_node(enkf_state->ensemble_config , key);
      enkf_state_add_node(enkf_state , key , config_node); 
    }
    
    enkf_node = hash_get(enkf_state->node_hash , key);
    var_type  = enkf_node_get_var_type( enkf_node );
    
    if (var_type == STATIC_STATE)
      enkf_fs_fread_node(shared_info->fs , enkf_node , report_step , iens , load_state);
  }


  /* Second pass - DYNAMIC state nodes. */
  {
    const int num_keys = hash_get_size(enkf_state->node_hash);
    char ** key_list   = hash_alloc_keylist(enkf_state->node_hash);
    int ikey;
    
    for (ikey = 0; ikey < num_keys; ikey++) {
      enkf_node_type * enkf_node = hash_get(enkf_state->node_hash , key_list[ikey]);
      enkf_var_type var_type = enkf_node_get_var_type( enkf_node );
      if (var_type == DYNAMIC_STATE) {
        /* 
           Here the xxx_try_fread() function is used NOT because we accept
           that the node is not present, but because the try_fread()
           function accepts the BOTH state type.
        */
        if (!enkf_fs_try_fread_node(shared_info->fs , enkf_node , report_step , iens , BOTH)) 
          util_abort("%s: failed to load node:%s  report_step:%d iens:%d \n",__func__ , key_list[ikey] , report_step , iens  );
      }
    }
    util_free_stringlist(key_list , num_keys);    
  }
}



/**
   This is a special function which is only used to load the initial
   state of dynamic_state nodes. It checks if the enkf_config_node has
   set a valid value for input_file, in that case that means we should
   also have an internalized representation of it, otherwise it will
   just return (i.e. for PRESSURE / SWAT).
*/

static void enkf_state_fread_initial_state(enkf_state_type * enkf_state) {
  shared_info_type * shared_info = enkf_state->shared_info;
  const member_config_type * my_config = enkf_state->my_config;
  const int num_keys = hash_get_size(enkf_state->node_hash);
  char ** key_list   = hash_alloc_keylist(enkf_state->node_hash);
  int ikey;
  
  for (ikey = 0; ikey < num_keys; ikey++) {
    enkf_node_type * enkf_node = hash_get(enkf_state->node_hash , key_list[ikey]);
    if (enkf_node_get_var_type(enkf_node) == DYNAMIC_STATE) {
      const enkf_config_node_type * config_node = enkf_node_get_config( enkf_node );

      /* Just checked for != NULL */
      char * load_file = enkf_config_node_alloc_infile( config_node , 0);
      if (load_file != NULL) 
	enkf_fs_fread_node(shared_info->fs , enkf_node , 0 , member_config_get_iens( my_config ) , ANALYZED);

      util_safe_free( load_file );
    }
  }                                                                     
  util_free_stringlist(key_list , num_keys);
}


void enkf_state_free_nodes(enkf_state_type * enkf_state, int mask) {
  const int num_keys = hash_get_size(enkf_state->node_hash);
  char ** key_list   = hash_alloc_keylist(enkf_state->node_hash);
  int ikey;
  
  for (ikey = 0; ikey < num_keys; ikey++) {
    enkf_node_type * enkf_node = hash_get(enkf_state->node_hash , key_list[ikey]);
    if (enkf_node_include_type(enkf_node , mask)) 
      enkf_state_del_node(enkf_state , enkf_node_get_key(enkf_node));
  }                                                                     
  util_free_stringlist(key_list , num_keys);
}

      




void enkf_state_free(enkf_state_type *enkf_state) {
  hash_free(enkf_state->node_hash);
  subst_list_free(enkf_state->subst_list);
  stringlist_free(enkf_state->restart_kw_list);
  member_config_free(enkf_state->my_config);
  run_info_free(enkf_state->run_info);
  shared_info_free(enkf_state->shared_info);
  free(enkf_state);
}



enkf_node_type * enkf_state_get_node(const enkf_state_type * enkf_state , const char * node_key) {
  if (hash_has_key(enkf_state->node_hash , node_key)) {
    enkf_node_type * enkf_node = hash_get(enkf_state->node_hash , node_key);
    return enkf_node;
  } else {
    util_abort("%s: node:%s not found in state object - aborting \n",__func__ , node_key);
    return NULL; /* Compiler shut up */
  }
}



void enkf_state_del_node(enkf_state_type * enkf_state , const char * node_key) {
  if (hash_has_key(enkf_state->node_hash , node_key)) 
    hash_del(enkf_state->node_hash , node_key);
  else 
    fprintf(stderr,"%s: tried to remove node:%s which is not in state - internal error?? \n",__func__ , node_key);
}


/**
   This function will set all the subst_kw key=value pairs which
   change with report step.
*/

static void enkf_state_set_dynamic_subst_kw(enkf_state_type * enkf_state , const char * run_path , int step1 , int step2) {
  const bool fmt_file      = ecl_config_get_formatted(enkf_state->ecl_config);
  const char * eclbase     = member_config_get_eclbase( enkf_state->my_config );
  char * step1_s 	   = util_alloc_sprintf("%d" , step1);
  char * step2_s 	   = util_alloc_sprintf("%d" , step2);
  char * step1_s04 	   = util_alloc_sprintf("%04d" , step1);
  char * step2_s04 	   = util_alloc_sprintf("%04d" , step2);
  char * restart_file1     = ecl_util_alloc_filename(NULL , eclbase , ECL_RESTART_FILE , fmt_file , step1);
  char * restart_file2     = ecl_util_alloc_filename(NULL , eclbase , ECL_RESTART_FILE , fmt_file , step2);
  
  enkf_state_add_subst_kw(enkf_state , "RUNPATH"       , run_path      , NULL);
  enkf_state_add_subst_kw(enkf_state , "TSTEP1"        , step1_s       , NULL);
  enkf_state_add_subst_kw(enkf_state , "TSTEP2"        , step2_s       , NULL);
  enkf_state_add_subst_kw(enkf_state , "TSTEP1_04"     , step1_s04     , NULL);
  enkf_state_add_subst_kw(enkf_state , "TSTEP2_04"     , step2_s04     , NULL);
  enkf_state_add_subst_kw(enkf_state , "RESTART_FILE1" , restart_file1 , NULL);
  enkf_state_add_subst_kw(enkf_state , "RESTART_FILE2" , restart_file2 , NULL);

  /**
     The <INIT> magic string:
  */
  if (step1 == 0) {
    const char * init_file = ecl_config_get_equil_init_file(enkf_state->ecl_config);
    if (init_file != NULL) {
      char * tmp_include = util_alloc_sprintf("INCLUDE\n   \'%s\' /\n",init_file);
      enkf_state_add_subst_kw(enkf_state , "INIT" , tmp_include , NULL);
      free(tmp_include);
    } /* 
         if init_file == NULL that means the user has not supplied the INIT_SECTION keyword, 
         and the EQUIL (or whatever) info to initialize the model is inlined in the datafile.
      */
  } else {
    char * data_initialize = util_alloc_sprintf("RESTART\n   \'%s\'  %d  /\n" , eclbase , step1);
    enkf_state_add_subst_kw(enkf_state , "INIT" , data_initialize , NULL);
    free(data_initialize);
  }
  
  
  {
    /** 
        Adding keys for <RANDINT> and <RANDFLOAT> - these are only
         added for backwards compatibility, should be replaced with
        prober function callbacks.
    */
    char * randint_value    = util_alloc_sprintf( "%d"      , rand());
    char * randfloat_value  = util_alloc_sprintf( "%12.10f" , 1.0 * rand() / RAND_MAX);
    
    enkf_state_add_subst_kw( enkf_state , "RANDINT"   , randint_value   , NULL);
    enkf_state_add_subst_kw( enkf_state , "RANDFLOAT" , randfloat_value , NULL);
    
    free( randint_value );
    free( randfloat_value );
  }



  free(step1_s);
  free(step2_s);
  free(step1_s04);
  free(step2_s04);
  free(restart_file1);
  free(restart_file2);
}



void enkf_state_printf_subst_list(enkf_state_type * enkf_state , int step1 , int step2) {
  int ikw;
  const char * fmt_string = "%-16s %-40s :: %s\n";
  printf("\n\n");
  printf(fmt_string , "Key" , "Current value" , "Description");
  printf("------------------------------------------------------------------------------------------------------------------------\n");
  if (step1 >= 0)
    enkf_state_set_dynamic_subst_kw(enkf_state , NULL , step1 , step2);

  for (ikw = 0; ikw < subst_list_get_size( enkf_state->subst_list ); ikw++) {
    const char * key   = subst_list_iget_key( enkf_state->subst_list , ikw);
    const char * value = subst_list_iget_value( enkf_state->subst_list , ikw);
    const char * desc  = subst_list_iget_doc_string( enkf_state->subst_list , ikw );
    
    if (value != NULL)
      printf(fmt_string , key , value , desc);
    else
      printf(fmt_string , key , "[Not set]" , desc);
  }
  printf("------------------------------------------------------------------------------------------------------------------------\n");
  
}




/**
   init_step    : The parameters are loaded from this EnKF/report step.
   report_step1 : The simulation should start from this report step; 
                  dynamic data are loaded from this step.
   report_step2 : The simulation should stop at this report step. (unless run_mode == ENSEMBLE_PREDICTION - where it just runs til end.)

   For a normal EnKF run we well have init_step == report_step1, but
   in the case where we want rerun from the beginning with updated
   parameters, they will be different. If init_step != report_step1,
   it is required that report_step1 == 0; otherwise the dynamic data
   will become completely inconsistent. We just don't allow that!
*/


static void enkf_state_init_eclipse(enkf_state_type *enkf_state) {
  const member_config_type  * my_config = enkf_state->my_config;  
  {
    const run_info_type * run_info    = enkf_state->run_info;
    if (!run_info->__ready) 
      util_abort("%s: must initialize run parameters with enkf_state_init_run() first \n",__func__);
    
    if (member_config_pre_clear_runpath( my_config )) 
      util_clear_directory( run_info->run_path , true , false );

    util_make_path(run_info->run_path);
    {
  
      char * schedule_file = util_alloc_filename(run_info->run_path , ecl_config_get_schedule_target( enkf_state->ecl_config ) , NULL);
      bool   addEND;
      if (run_info->run_mode == ENSEMBLE_PREDICTION)
        addEND = false;
      else
        addEND = true;
      
      sched_file_fprintf_i( ecl_config_get_sched_file( enkf_state->ecl_config ) , run_info->step2 , schedule_file , addEND);
      free(schedule_file);
    }


    /**
       For reruns of various kinds the parameters and the state are
       generally loaded from different timesteps:
    */

    /* Loading parameter information: loaded from timestep: run_info->init_step_parameters. */
    enkf_state_fread(enkf_state , PARAMETER , run_info->init_step_parameters , run_info->init_state_parameter);
    

    /* Loading state information: loaded from timestep: run_info->step1 */
    if (run_info->step1 == 0)
      enkf_state_fread_initial_state(enkf_state); 
    else
      enkf_state_fread_state_nodes( enkf_state , run_info->step1 , run_info->init_state_dynamic);

    enkf_state_set_dynamic_subst_kw(  enkf_state , run_info->run_path , run_info->step1 , run_info->step2);
    ert_templates_instansiate( enkf_state->shared_info->templates , run_info->run_path , enkf_state->subst_list );
    enkf_state_ecl_write( enkf_state );
    
    {
      char * stdin_file = util_alloc_filename(run_info->run_path , "eclipse"  , "stdin");  /* The name eclipse.stdin must be mathched when the job is dispatched. */
      ecl_util_init_stdin( stdin_file , member_config_get_eclbase( my_config ));
      free(stdin_file);
    }
    
    /* Writing the ECLIPSE data file. */
    {
      char * data_file = ecl_util_alloc_filename(run_info->run_path , member_config_get_eclbase( my_config ) , ECL_DATA_FILE , true , -1);
      subst_list_filter_file(enkf_state->subst_list , ecl_config_get_data_file(enkf_state->ecl_config) , data_file);
      free( data_file );
    }
    
    /* This is where the job script is created */
    forward_model_python_fprintf( model_config_get_forward_model( enkf_state->shared_info->model_config ) , run_info->run_path , enkf_state->subst_list);
  }
}






/**
   xx_run_forward_model() has been split in two functions:

   1: enkf_state_start_forward_model()

   2: enkf_state_complete_forward_model()

   Because the first is quite CPU intensive (gunzip), and the number of
   concurrent threads should be limited. For the second there is one
   thread for each ensemble member. This is handled by the calling scope.
*/



static void enkf_state_start_forward_model(enkf_state_type * enkf_state) {
  run_info_type       * run_info    = enkf_state->run_info;
  if (run_info->active) {  /* if the job is not active we just return .*/
    const shared_info_type    * shared_info   = enkf_state->shared_info;
    const member_config_type  * my_config     = enkf_state->my_config;
    const forward_model_type  * forward_model = model_config_get_forward_model(shared_info->model_config);
    /*
      Prepare the job and submit it to the queue
    */
    enkf_state_init_eclipse(enkf_state);
    job_queue_insert_job( shared_info->job_queue , 
                          run_info->run_path , 
                          member_config_get_eclbase(my_config) , 
                          member_config_get_iens(my_config) , 
                          NULL );
    run_info->num_internal_submit++;
  }
}


/** 
    This function is called when:

     1. The external queue system has said that everything is OK; BUT
        the ert layer failed to load all the data.
    
     2. The external queue system has seen the job fail.
    
    The parameter and state variables will be resampled before
    retrying. And all random elements in templates+++ will be
    resampled.
*/

static bool enkf_state_internal_retry(enkf_state_type * enkf_state , bool load_failure) {
  const member_config_type  * my_config   = enkf_state->my_config;
  run_info_type             * run_info    = enkf_state->run_info;
  const shared_info_type    * shared_info = enkf_state->shared_info;
  const int iens                          = member_config_get_iens( my_config );

  if (load_failure)
    log_add_fmt_message(shared_info->logh , 1 , NULL , "[%03d:%04d-%04d] Failed to load all data.",iens , run_info->step1 , run_info->step2);
  else
    log_add_fmt_message(shared_info->logh , 1 , NULL , "[%03d:%04d-%04d] Forward model failed.",iens, run_info->step1 , run_info->step2);

  if (run_info->num_internal_submit < run_info->max_internal_submit) {
    log_add_fmt_message( shared_info->logh , 1 , NULL , "[%03d] Resampling and resubmitting realization." ,iens);
    {
      /* Reinitialization of the nodes */
      stringlist_type * init_keys = ensemble_config_alloc_keylist_from_var_type( enkf_state->ensemble_config , DYNAMIC_STATE + PARAMETER );
      for (int ikey=0; ikey < stringlist_get_size( init_keys ); ikey++) {
        enkf_node_type * node = enkf_state_get_node( enkf_state , stringlist_iget( init_keys , ikey) );
        enkf_node_initialize( node , iens );
      }
      stringlist_free( init_keys );
    }
    
    enkf_state_init_eclipse( enkf_state );                              /* Possibly clear the directory and do a FULL rewrite of ALL the necessary files. */
    job_queue_set_external_restart( shared_info->job_queue , iens );    /* Here we inform the queue system that it should pick up this job and try again. */
    run_info->num_internal_submit++;                                    
    return true;
  } else 
    return false;                                                       /* No more internal retries. */

}



job_status_type enkf_state_get_run_status( const enkf_state_type * enkf_state ) {
  run_info_type             * run_info    = enkf_state->run_info;
  if (run_info->active) {
    const shared_info_type    * shared_info = enkf_state->shared_info;
    return job_queue_get_job_status(shared_info->job_queue , member_config_get_iens( enkf_state->my_config ));
  } else
    return JOB_QUEUE_NOT_ACTIVE;
}


time_t enkf_state_get_start_time( const enkf_state_type * enkf_state ) {
  run_info_type             * run_info    = enkf_state->run_info;
  if (run_info->active) {
    const shared_info_type    * shared_info = enkf_state->shared_info;
    return job_queue_iget_sim_start(shared_info->job_queue , member_config_get_iens( enkf_state->my_config ));
  } else
    return -1;
}


time_t enkf_state_get_submit_time( const enkf_state_type * enkf_state ) {
  run_info_type             * run_info    = enkf_state->run_info;
  if (run_info->active) {
    const shared_info_type * shared_info = enkf_state->shared_info;
    return job_queue_iget_submit_time(shared_info->job_queue , member_config_get_iens( enkf_state->my_config ));
  } else
    return -1;
}




/**
   Will return true if the simulation is actually killed, and false if
   the "kill command" is ignored (only jobs with status matching
   JOB_QUEUE_CAN_KILL will actually be killed).
*/

bool enkf_state_kill_simulation( const enkf_state_type * enkf_state ) {
  const shared_info_type * shared_info = enkf_state->shared_info;
  return job_queue_kill_job(shared_info->job_queue , member_config_get_iens( enkf_state->my_config ));
}


/**
   This function is very similar to the enkf_state_internal_retry() -
   they should be refactored.

   Will return true if the simulation is actually resubmitted, and
   false if it is not restarted.
*/

bool enkf_state_resubmit_simulation( enkf_state_type * enkf_state , bool resample) {
  const shared_info_type * shared_info = enkf_state->shared_info;
  int iens                       = member_config_get_iens( enkf_state->my_config );
  job_status_type current_status = job_queue_get_job_status(shared_info->job_queue , member_config_get_iens( enkf_state->my_config ));
  if (current_status & JOB_QUEUE_CAN_RESTART) { 
    /* Reinitialization of the nodes */
    if (resample) {
      stringlist_type * init_keys = ensemble_config_alloc_keylist_from_var_type( enkf_state->ensemble_config , DYNAMIC_STATE + PARAMETER );
      for (int ikey=0; ikey < stringlist_get_size( init_keys ); ikey++) {
        enkf_node_type * node = enkf_state_get_node( enkf_state , stringlist_iget( init_keys , ikey) );
        enkf_node_initialize( node , iens );
      }
      stringlist_free( init_keys );
    }
    enkf_state_init_eclipse( enkf_state );                              /* Possibly clear the directory and do a FULL rewrite of ALL the necessary files. */
    job_queue_set_external_restart( shared_info->job_queue , iens );    /* Here we inform the queue system that it should pick up this job and try again. */
    return true;
  } else
    return false; /* The job was not resubmitted. */
}




/** 
    Observe that if run_info == false, this routine will return with
    job_completeOK == true, that might be a bit misleading.
    
    Observe that if an internal retry is performed, this function will
    be called several times - MUST BE REENTRANT.
*/

static void enkf_state_complete_forward_model(enkf_state_type * enkf_state , job_status_type status) {
  const shared_info_type    * shared_info = enkf_state->shared_info;
  run_info_type             * run_info    = enkf_state->run_info;
  const member_config_type  * my_config   = enkf_state->my_config;
  const int iens                          = member_config_get_iens( my_config );
  bool loadOK  = true;

  job_queue_set_external_load( shared_info->job_queue , iens );  /* Set the the status of to JOB_QUEUE_LOADING */
  if (status == JOB_QUEUE_RUN_OK) {
    /**
       The queue system has reported that the run is OK, i.e. it has
       completed and produced the targetfile it should. We then check
       in this scope whether the results can be loaded back; if that
       is OK the final status is updated, otherwise: restart.
    */
    log_add_fmt_message( shared_info->logh , 2 , NULL , "[%03d:%04d-%04d] Forward model complete - starting to load results." , iens , run_info->step1, run_info->step2);
    enkf_state_internalize_results(enkf_state , &loadOK); 
    if (loadOK) {
      status = JOB_QUEUE_ALL_OK;
      job_queue_set_load_OK( shared_info->job_queue , iens );
      log_add_fmt_message( shared_info->logh , 2 , NULL , "[%03d:%04d-%04d] Results loaded successfully." , iens , run_info->step1, run_info->step2);
    } else 
      if (!enkf_state_internal_retry( enkf_state , true)) 
        /* 
           We tell the queue system that the job failed to load the
           data; it will immediately come back here with status
           job_queue_run_FAIL and then it falls all the way through to
           runOK = false and no more attempts.
        */
        job_queue_set_external_fail( shared_info->job_queue , iens );
  } else if (status == JOB_QUEUE_RUN_FAIL) {
    /* 
       The external queue system has said that the job failed - we
       give it another try from this scope, possibly involving a
       resampling.
    */
    if (!enkf_state_internal_retry( enkf_state , false)) {
      run_info->runOK = false; /* OK - no more attempts. */
      log_add_fmt_message( shared_info->logh , 1 , NULL , "[%03d:%04d-%04d] FAILED COMPLETELY." , iens , run_info->step1, run_info->step2);
      /* We tell the queue system that we have really given up on this one */
      job_queue_set_all_fail( shared_info->job_queue , iens );
    } 
  } else 
    util_abort("%s: status:%d invalid - should only be called with status == [JOB_QUEUE_RUN_FAIL || JON_QUEUE_ALL_OK].",__func__ , status);
  
  /* 
     Results have been successfully loaded, and we clear up the run
     directory. Or alternatively the job has failed completely, and we
     just leave the runpath directory hanging around.
  */

    
  /* 
     In case the job fails, we leave the run_path directory around
     for debugging. 
  */
  if (status == JOB_QUEUE_ALL_OK) {
    keep_runpath_type keep_runpath = member_config_get_keep_runpath( my_config );
    bool unlink_runpath;
    if (keep_runpath == DEFAULT_KEEP) {
      if (run_info->run_mode == ENKF_ASSIMILATION)
        unlink_runpath = true;   /* For assimilation the default is to unlink. */
      else
        unlink_runpath = false;  /* For experiments the default is to keep the directories around. */
    } else {
      /* We have explcitly set a value for the keep_runpath variable - with either KEEP_RUNAPTH or DELETE_RUNPATH. */
      if (keep_runpath == EXPLICIT_KEEP)
        unlink_runpath = false;
      else if (keep_runpath == EXPLICIT_DELETE)
        unlink_runpath = true;
      else {
        util_abort("%s: internal error \n",__func__);
        unlink_runpath = false; /* Compiler .. */
      }
    }
    if (unlink_runpath)
      util_clear_directory(run_info->run_path , true , true);
    
    run_info->runOK   = true;
    run_info->__ready = false;                    /* Setting it to false - for the next round ??? */
    run_info_complete_run(enkf_state->run_info);  /* free() on runpath */
  }
}


void * enkf_state_complete_forward_model__(void * arg ) {
  arg_pack_type * arg_pack = arg_pack_safe_cast( arg );
  enkf_state_type * enkf_state = arg_pack_iget_ptr( arg_pack , 0 );
  job_status_type   status     = arg_pack_iget_int( arg_pack , 1 );

  enkf_state_complete_forward_model( enkf_state , status );
  return NULL;
}


/**
   Checks that both the run has completed OK - that also includes the
   loading back of results.
*/
   
bool enkf_state_runOK(const enkf_state_type * state) {
  return state->run_info->runOK;
}




void * enkf_state_start_forward_model__(void * __enkf_state) {
  enkf_state_type * enkf_state = enkf_state_safe_cast(__enkf_state);
  enkf_state_start_forward_model( enkf_state );
  return NULL ; 
}


void enkf_state_invalidate_cache( enkf_state_type * enkf_state ) {
  hash_iter_type * iter       = hash_iter_alloc(enkf_state->node_hash);
  while ( !hash_iter_is_complete(iter) ) {
    enkf_node_type * node = hash_iter_get_next_value(iter);
    enkf_node_invalidate_cache( node );
  }
  hash_iter_free(iter);
}




/*****************************************************************/


void enkf_state_set_inactive(enkf_state_type * state) {
  state->run_info->active = false;
}


void enkf_state_init_run(enkf_state_type * state , 
                         run_mode_type run_mode  , 
                         bool active                    , 
                         int max_internal_submit,
                         int init_step_parameter         , 
                         state_enum init_state_parameter , 
                         state_enum init_state_dynamic   , 
                         int load_start          , 
                         int step1               , 
                         int step2) {

  member_config_type * my_config    = state->my_config;
  shared_info_type   * shared_info  = state->shared_info;

  
  run_info_set( state->run_info , 
                run_mode        , 
                active          , 
                max_internal_submit,
                init_step_parameter , 
                init_state_parameter , 
                init_state_dynamic  , 
                load_start , 
                step1 , 
                step2 , 
                member_config_get_iens( my_config ), 
                model_config_get_runpath_fmt( shared_info->model_config ),
                state->subst_list );
}



/*****************************************************************/


/**
   This function will return the true time (i.e. time_t instance)
   cooresponding to the report_step 'report_step'. If no data has been
   loaded for the input report_step -1 is returned, this must be
   checked by the calling scope.
*/

time_t enkf_state_iget_sim_time( const enkf_state_type * enkf_state , int report_step ) {
  return member_config_iget_sim_time(enkf_state->my_config , report_step , NULL);
}


bool enkf_state_has_report_step( const enkf_state_type * enkf_state , int report_step) {
  return member_config_has_report_step(enkf_state->my_config , report_step );
}


void enkf_state_set_keep_runpath( enkf_state_type * enkf_state , keep_runpath_type keep_runpath) {
  member_config_set_keep_runpath( enkf_state->my_config , keep_runpath);
}


keep_runpath_type enkf_state_get_keep_runpath( const enkf_state_type * enkf_state ) {
  return member_config_get_keep_runpath( enkf_state->my_config );
}
