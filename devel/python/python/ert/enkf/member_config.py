#  Copyright (C) 2012  Statoil ASA, Norway. 
#   
#  The file 'member_config.py' is part of ERT - Ensemble based Reservoir Tool. 
#   
#  ERT is free software: you can redistribute it and/or modify 
#  it under the terms of the GNU General Public License as published by 
#  the Free Software Foundation, either version 3 of the License, or 
#  (at your option) any later version. 
#   
#  ERT is distributed in the hope that it will be useful, but WITHOUT ANY 
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or 
#  FITNESS FOR A PARTICULAR PURPOSE.   
#   
#  See the GNU General Public License at <http://www.gnu.org/licenses/gpl.html> 
#  for more details. 

import  ctypes
from    ert.cwrap.cwrap       import *
from    ert.cwrap.cclass      import CClass
from    ert.util.tvector      import * 
from    enkf_enum             import *
import  libenkf
class MemberConfig(CClass):
    
    def __init__(self , c_ptr = None):
        self.owner = False
        self.c_ptr = c_ptr
        
        
    def __del__(self):
        if self.owner:
            cfunc.free( self )


    def has_key(self , key):
        return cfunc.has_key( self ,key )



##################################################################

cwrapper = CWrapper( libenkf.lib )
cwrapper.registerType( "member_config" , MemberConfig )

# 3. Installing the c-functions used to manipulate ecl_kw instances.
#    These functions are used when implementing the EclKW class, not
#    used outside this scope.
cfunc = CWrapperNameSpace("member_config")


cfunc.free                = cwrapper.prototype("void member_config_free( member_config )")
cfunc.iget_sim_days       = cwrapper.prototype("double member_config_iget_sim_days(member_config, int, int)")
cfunc.iget_sim_time       = cwrapper.prototype("time_t member_config_iget_sim_time(member_config, int, int)")

