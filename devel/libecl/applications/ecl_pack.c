#include <util.h>
#include <ecl_file.h>
#include <ecl_util.h>
#include <stdlib.h>
#include <msg.h>
#include <ecl_endian_flip.h>   
#include <stringlist.h>


int main(int argc, char ** argv) {
  int num_files = argc - 1;
  if (num_files >= 1) {
    /* File type and formatted / unformatted is determined from the first argument on the command line. */
    char * ecl_base;
    char * path;
    ecl_file_enum file_type , target_type;
    bool fmt_file;

    /** Look at the first command line argument to determine type and formatted/unformatted status. */
    ecl_util_get_file_type( argv[1] , &file_type , &fmt_file , NULL);
    if (file_type == ECL_SUMMARY_FILE)
      target_type = ECL_UNIFIED_SUMMARY_FILE;
    else if (file_type == ECL_RESTART_FILE)
      target_type = ECL_UNIFIED_RESTART_FILE;
    else {
      util_exit("The ecl_pack program can only be used with ECLIPSE restart files or summary files.\n");
      target_type = -1;
    }
    util_alloc_file_components( argv[1] , &path , &ecl_base , NULL);

    
    {
      msg_type * msg;
      int i , report_step , prev_report_step;
      char *  target_file_name   = ecl_util_alloc_filename( path , ecl_base , target_type , fmt_file , -1);
      stringlist_type * filelist = stringlist_alloc_argv_copy( (const char **) &argv[1] , num_files );
      ecl_kw_type * seqnum_kw    = NULL;
      fortio_type * target       = fortio_fopen( target_file_name , "w" , ECL_ENDIAN_FLIP , fmt_file );

      if (target_type == ECL_UNIFIED_RESTART_FILE) {
	int dummy;
	seqnum_kw = ecl_kw_alloc_new("SEQNUM" , 1 , ECL_INT_TYPE , &dummy);
      } 
      
      {
	char * msg_format = util_alloc_sprintf("Packing %s <= " , target_file_name);
	msg = msg_alloc( msg_format );
	free( msg_format );
      }
      
      msg_show( msg );
      stringlist_sort( filelist , ecl_util_fname_report_cmp);
      prev_report_step = -1;
      for (i=0; i < num_files; i++) {
	ecl_file_enum this_file_type;
	ecl_util_get_file_type( stringlist_iget(filelist , i)  , &this_file_type , NULL , &report_step);
	if (this_file_type == file_type) {
	  if (report_step == prev_report_step)
	    util_exit("Tried to write same report step twice: %s / %s \n",
                      stringlist_iget(filelist , i-1) , 
                      stringlist_iget(filelist , i));

	  prev_report_step = report_step;
	  msg_update(msg , stringlist_iget( filelist , i));
	  {
	    ecl_file_type * src_file = ecl_file_fread_alloc( stringlist_iget( filelist , i) );
	    if (target_type == ECL_UNIFIED_RESTART_FILE) {
	      /* Must insert the SEQNUM keyword first. */
	      ecl_kw_iset_int(seqnum_kw , 0 , report_step);
	      ecl_kw_fwrite( seqnum_kw , target );
	    }
	    ecl_file_fwrite_fortio( src_file , target , 0);
	    ecl_file_free( src_file );
	  }
	}  /* Else skipping file of incorrect type. */
      }
      msg_free(msg , false);
      fortio_fclose( target );
      free(target_file_name);
      stringlist_free( filelist );
      if (seqnum_kw != NULL) ecl_kw_free(seqnum_kw);
    }
    free(ecl_base);
    util_safe_free(path);
  }
}
  
