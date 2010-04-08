
from ctypes import *
import ctypes.util


class ErtWrapper:

    def __init__(self, site_config="/project/res/etc/ERT/Config/site-config", enkf_config="/private/jpb/EnKF/Testcases/SimpleEnKF/enkf_config", enkf_so="/private/jpb/EnKF/"):
        self.__loadLibraries__(enkf_so)

        #bootstrap
        self.main = self.enkf.enkf_main_bootstrap(site_config, enkf_config)
        self.plot_config = self.enkf.enkf_main_get_plot_config(self.main)
        self.analysis_config = self.enkf.enkf_main_get_analysis_config(self.main)
        self.ecl_config = self.enkf.enkf_main_get_ecl_config(self.main)

        

#        self.plot_path = "somepath"
#        self.driver = "PLPLOT"
#        self.errorbar = 1256
#        self.width = 1920
#        self.height = 1080
#        self.image_viewer = "another path"
#        self.image_type = "png"

        #self.eclbase="eclpath"
        #self.data_file="eclpath"
        #self.grid="eclpath"
        ###self.schedule_file="eclpath"
        #self.init_section="init_section"
        self.add_fixed_length_schedule_kw = ["item1", "item2"]
        self.add_static_kw = ["item1", "item2", "item3"]

#Get: ecl_config_get_static_kw_list( ecl_config )
#Set: ecl_config_clear_static_kw(ecl_config)
#     ecl_config_add_static_kw(ecl_config , "STRING")


        #self.refcase = "some new path"
        self.schedule_prediction_file = "Missing???"
        self.data_kw = {"INCLUDE_PATH" : "<CWD>/../Common/ECLIPSE2", "INCLUDE_PATH2" : "<CWD>/../Common/ECLIPSE2", "INCLUDE_PATH3" : "<CWD>/../Common/ECLIPSE2"}
#Get: s = enkf_main_get_data_kw( enkf_main )
#     subst_list_get_size( s )
#     subst_list_iget_key( s )  
#     subst_list_iget_value( s )
#Set: enkf_main_clear_data_kw( enkf_main )
#     enkf_main_add_data_kw( enkf_main , key , value )




#        self.enkf_rerun = False
#        self.rerun_start = 0
        self.enkf_sched_file = "..."
#Get: m = enkf_main_get_model_config( enkf_main )
#         model_config_get_enkf_sched_file( m )
#Set:     model_config_set_enkf_sched_file( m , "FILENAME" )


        self.local_config = "..."
#        self.enkf_merge_observations = False
#        self.enkf_mode = "SQRT"
#        self.enkf_alpha = 2.5
#        self.enkf_truncation = 0.99



        self.queue_system = "RSH"
        self.lsf_queue = "NORMAL"
        self.max_running_lsf = 10
        self.lsf_resources = "magic string"
        self.rsh_command = "ssh"
        self.max_running_rsh = 55
        self.max_running_local = 4
        self.rsh_host_list = {"host1" : '', "host2" : '6'}
#Get    s = enkf_main_get_site_config( enkf_main )
#       site_config_get_max_running_(lsf|rsh|local)( s )
#Set    site_config_get_max_running_(lsf|rsh|local)( s , value )

#Get    s = enkf_main_get_site_config( enkf_main )
#       h = site_config_get_rsh_host_list( s )
#       Itererer over hash - men bruk hash_get_int() for � f� antall jobber en host kan ta.
#Set    site_config_clear_rsh_host_list( s )
#       site_config_add_rsh_host( s , host_name , max_running )

 

        self.job_script = "..."
        self.setenv = {"LSF_BINDIR" : "/prog/LSF/7.0/linux2.6-glibc2.3-x86_64/bin", "LSF_LIBDIR" : "/prog/LSF/7.0/linux2.6-glibc2.3-x86_64/lib"}
#Get:   s = enkf_main_get_site_config( enkf_main )
#       h = site_config_get_env_hash( s )    
#Set    site_config_clear_env( s )
#       site_config_setenv( s , var , value )

        self.update_path = {"PATH" : "/prog/LSF/7.0/linux2.6-glibc2.3-x86_64/bin", "LD_LIBRARY_PATH" : "/prog/LSF/7.0/linux2.6-glibc2.3-x86_64/lib"}
#Get:   s = enkf_main_get_site_config( enkf_main )
#       pathlist  = site_config_get_path_variables( s )
#       valuelist = site_config_get_path_values( s )
#Set:   site_config_clear_pathvar( s )
#       site_config_update_pathvar( s , path , value );


        self.install_job = {"ECHO" : "/prog/LSF/7.0/linux2.6-glibc2.3-x86_64/bin", "ADJUSTGRID" : "/prog/LSF/7.0/linux2.6-glibc2.3-x86_64/lib"}


        self.dbase_type = "BLOCK_FS"
        self.enspath = "storage"
        self.select_case = "some_case"

        self.log_file = "log"
        self.log_level = 1
        self.update_log_path = "one path"

        self.history_source = "REFCASE_HISTORY"
        self.obs_config = "..."

        self.runpath = "simulations/realization%d"
        self.pre_clear_runpath = True
        self.delete_runpath = "0 - 10, 12, 15, 20"
        self.keep_runpath = "0-15, 18, 20"

        self.license_path = "/usr"
        self.max_submit = 2
        self.max_resample = 16
        self.case_table = "..."

        self.run_template = [["...", ".....", "asdf:asdf asdfasdf:asdfasdf"], ["other", "sdtsdf", ".as.asdfsdf"]]
#        self.run_template = "..."
#        self.target_file = "..."
#        self.template_arguments = {"HABBA":"sdfkjskdf/sdf"}
        self.forward_model = {"MY_RELPERM_SCRIPT":"Arg1<some> COPY(asdfdf)"}

        self.field_dynamic = [["PRESSURE", "", ""], ["SWAT", 0.1, 0.95], ["SGAS", "", 0.25]]
        self.field_parameter = [["PRESSURE", "", "", "RANDINT", "EXP", "...", "/path/%d"], ["SWAT", 0.1, 0.95, "", "", ".", "..."]]
        self.field_general = [["PERMEABILITY", "", "", "RANDINT", "EXP", "...", "/path/%d", "path to somewhere", "another path"]]

        self.gen_data = [["5DWOC", "SimulatedWOCCA.txt", "ASCII", "BINARY_DOUBLE", "---", "/path/%d"]]
        self.gen_kw = [["MULTFLT", "Templates/MULTFLT_TEMPLATE", "MULTFLT.INC", "Parameters/MULTFLT.txt"]]
        self.gen_param = [["DRIBBLE", "ASCII", "ASCII_TEMPLATE", "...", "/path/%d", "/some/file Magic123"]]

        self.num_realizations = 100
        self.summary = ["WPOR:MY_WELL", "RPR:8", "F*"]





    def __loadLibraries__(self, prefix):
        libraries = ["libecl/slib/libecl.so",
                     "libsched/slib/libsched.so",
                     "librms/slib/librms.so",
                     "libconfig/slib/libconfig.so",
                     "libjob_queue/slib/libjob_queue.so"]

        CDLL("libblas.so", RTLD_GLOBAL)
        CDLL("liblapack.so", RTLD_GLOBAL)
        CDLL("libz.so", RTLD_GLOBAL)

        self.util = CDLL(prefix + "libutil/slib/libutil.so", RTLD_GLOBAL)
        
        for lib in libraries:
            CDLL(prefix + lib, RTLD_GLOBAL)

        self.enkf = CDLL(prefix + "libenkf/slib/libenkf.so", RTLD_GLOBAL)


    def setRestype(self, attribute, restype):
        getattr(self.enkf, attribute).restype = restype

    def setValueType(self, returnAttribute, setAttribute, type):
        getattr(self.enkf, returnAttribute).restype = type
        getattr(self.enkf, setAttribute).argtypes = [ctypes.c_int, type]

    def setAttribute(self, attribute, value):
        print "set " + attribute + ": " + str(getattr(self, attribute)) + " -> " + str(value)
        setattr(self, attribute, value)

    def getAttribute(self, attribute):
        print "get " + attribute + ": " + str(getattr(self, attribute))
        return getattr(self, attribute)

        
    def getStringList(self, stringlistpointer):
        result = []

        self.util.stringlist_iget.restype = c_char_p
        numberOfStrings = self.util.stringlist_get_size(stringlistpointer)

        for index in range(numberOfStrings):
            result.append(self.util.stringlist_iget(stringlistpointer, index))

        return result


    def freeStringList(self, stringlistpointer):
        self.util.stringlist_free(stringlistpointer)


    def getHash(self, hashpointer):
        hashiterator = self.util.hash_iter_alloc(hashpointer)
        self.util.hash_iter_get_next_key.restype = c_char_p
        self.util.hash_get.restype = c_char_p

        result = {}
        while not self.util.hash_iter_is_complete(hashiterator):
            key   = self.util.hash_iter_get_next_key(hashiterator)
            value = self.util.hash_get(hashpointer, key)
            result[key] = value

        self.util.hash_iter_free(hashiterator)
        return result
