// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
// vim:expandtab:tabstop=4:shiftwidth=4:softtabstop=4:

/*
 * Copyright (C) 2008 RobotCub Consortium, European Commission FP6 Project IST-004370
 * Author: Assif Mirza
 * email:   assif.mirza@robotcub.org
 * website: www.robotcub.org
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
 */

#include <iCub/iha/ShortTermMemoryModule.h>

#include <iCub/iha/iha_utils.h>
#include <iCub/iha/Actions.h>

using namespace iCub::iha;
using namespace std;

/**
 * @addtogroup icub_iha_ShortTermMemory
 *
\section intro_sec Description
Short Term Memory for IHA

\section lib_sec Libraries
- YARP libraries.
- IHA Debug Library

\section parameters_sec Parameters
\verbatim
--dbg <INT>                : debug printing level
--name <STR>               : process name for ports
--file <STR>               : configuration from given file

--connect_to_coords <STR>  : connect to specified port for face
\endverbatim

\section portsa_sec Ports Accessed

\section portsc_sec Ports Created

- /iha/sm/quit  - module quit port
 
\section conf_file_sec Configuration Files
conf/ihaShortTermMemory.ini

Sample INI file:
\verbatim
\endverbatim

\section tested_os_sec Tested OS
Linux

\section example_sec Example Instantiation of the Module
ihaShortTermMemory --file conf/ihaShortTermMemory.ini

\see iCub::contrib::ShortTermMemoryModule

\author Assif Mirza

Copyright (C) 2008 RobotCub Consortium

CopyPolicy: Released under the terms of the GNU GPL v2.0.

This file can be edited at \in src/interactionHistory/short_term_memory/src/ShortTermMemoryModule.cpp.
*/

ShortTermMemoryModule::ShortTermMemoryModule(){
}

ShortTermMemoryModule::~ShortTermMemoryModule(){ 
}


bool ShortTermMemoryModule::open(Searchable& config){
    
	if (config.check("dbg")) { IhaDebug::setLevel(config.find("dbg").asInt()); }
 	ACE_OS::fprintf(stderr, "Debug level : %d\n",IhaDebug::getLevel());
    
    if (config.check("help","if present, display usage message")) {
		cerr << "Usage : " << "\n"
             << "------------------------------------------" << "\n"
             << "  --dbg [INT]   : debug printing level" << "\n"
             << "  --name [STR]  : process name for ports" << "\n"
             << "  --file [STR]  : config file" << "\n"
             << "---------------------------------------------------------------------------" << "\n"
             << "  --connect_to_coords [STR]       : connect to specified port for face" << "\n"
             << "---------------------------------------------------------------------------" << "\n"
             << "\n";
        return false;
    }
    
    bool ok = true;
    
    // Read parameters
    //cycles/second
	resolution = config.check("resolution",Value(10)).asInt();
	IhaDebug::pmesg(DBGL_INFO,"resolution:%d\n",resolution);
    //seconds
	mem_length = config.check("mem_length",Value(4)).asInt();
	IhaDebug::pmesg(DBGL_INFO,"mem_length:%d\n",mem_length);
    
	// create names of ports
    ConstString coordsPortName = getName("data:in");
    ConstString outPortName = getName("score:out");
	
	// open the coordinates reading port
	coordsPort.open(coordsPortName.c_str());
	outPort.open(outPortName.c_str());
    
	// if required we can connect to the coordinates port
	if (config.check("connect_to_data")) {
		if (connectToParam(config,"connect_to_data",coordsPortName.c_str(), 0.25, this)) {
            IhaDebug::pmesg(DBGL_INFO,"Connected to Input Data\n");
        } else {
            ok = false;
        }
	}
    
    waitTime = 1.0/resolution;
    lastTime = 0;
    
    ok &= quitPort.open(getName("quit"));
    attach(quitPort, true);
    return ok;
    
    //We need to also open the iCub's action definitions,
    //so we can refer to the actions by name 
    //rather than a number that might change
    
    //------------------------------------------------------
	// get all the configured actions
	ConstString action_defs_file = config.check("action_defs",Value("conf/action_defs.ini")).asString();
	//ConstString sequence_directory = config.check("sequence_dir",Value(".")).asString();
    
	// create the action defs object and read the actions
	// from the config file
	Property actiondefs_props;
    Actions iCubActions;
	actiondefs_props.fromConfigFile(action_defs_file.c_str()); 
    
	if (!iCubActions.open(actiondefs_props)) {
		fprintf(stderr,"Error in action definitions\n");
		exit(-1);
	}
    
    //note: must match the name in action_defs.ini
    hide_act = iCubActions.getActionIndex("Hide-face");
    drum_act = iCubActions.getActionIndex("RArm-Drum");
    
    //store ways that we are going to process the data
    //in the short-term memory in order to determine a score
    vector<int> v;
    memory_process.insert(make_pair("robot_drum",v));
    memory_process.insert(make_pair("human_drum",v));
    memory_process.insert(make_pair("both_drum",v));
    memory_process.insert(make_pair("robot_hide",v));
    memory_process.insert(make_pair("human_hide",v));
    memory_process.insert(make_pair("both_hide",v));

    memory_sum.insert(make_pair("robot_drum", 0));
    memory_sum.insert(make_pair("human_drum", 0));
    memory_sum.insert(make_pair("both_drum", 0));
    memory_sum.insert(make_pair("robot_hide", 0));
    memory_sum.insert(make_pair("human_hide", 0));
    memory_sum.insert(make_pair("both_hide", 0));
    

}

bool ShortTermMemoryModule::close(){
    coordsPort.close();
    outPort.close();
    return true;
}

bool ShortTermMemoryModule::interruptModule(){
    coordsPort.interrupt();
    outPort.interrupt();
    return true;
}

bool ShortTermMemoryModule::updateModule(){
    
    //get the latest sensor reading from SMI
    Bottle* smUpdate = coordsPort.read();
    double currTime = Time::now();
    Bottle outMem;
    
    //if enough time has passed, store it
    if((currTime - lastTime) > waitTime) {
        lastTime = currTime;
        
        
        if(memory["action"].size() >= (resolution*mem_length)) {
            //discard the old memory before adding the new one
            map<string,std::vector<double> >::iterator iter;   
            for( iter = memory.begin(); iter != memory.end(); iter++ ) {
                iter->second.pop_back();
            }
        }
        //store the new data (notice that values are arranged new to old)
        for(int i = 0; i < smUpdate->size(); i=i+2) {
            std::string s =  string(smUpdate->get(i).asString().c_str()); 
            vector<double>::iterator iter = memory[s].begin();   
            memory[s].insert(iter,smUpdate->get(i+1).asDouble());
        }
        
        int curr_act = int(memory["action"].front());
        int human_drum = int(memory["sound"].front() > 0.5);
        int robot_drum = int(curr_act == drum_act);
        int both_drum = int(human_drum && robot_drum);
        int robot_hide = int(curr_act == hide_act);
        int human_hide =int(memory["face"].front() == 0.0);
        int both_hide = int(human_hide && robot_hide);
        
        
        if(memory_process["robot_drum"].size() >= (resolution*mem_length)) {
            //discard the old memory before adding the new one
            map<string,std::vector<int> >::iterator iter;   
            for( iter = memory_process.begin(); iter != memory_process.end(); iter++ ) {
                int tmp = iter->second.back();
                iter->second.pop_back();
                memory_sum[iter->first] = memory_sum[iter->first] - tmp;
            }
        }
        //store the new data (notice that values are arranged new to old)
        vector<int>::iterator iter = memory_process["robot_drum"].begin();   
        memory_process["robot_drum"].insert(iter,robot_drum);
        memory_sum["robot_drum"] = memory_sum["robot_drum"] + robot_drum;
        iter = memory_process["human_drum"].begin();   
        memory_process["human_drum"].insert(iter,human_drum);
        memory_sum["human_drum"] = memory_sum["human_drum"] + human_drum;
        iter = memory_process["both_drum"].begin();   
        memory_process["both_drum"].insert(iter,both_drum);
        memory_sum["both_drum"] = memory_sum["both_drum"] + both_drum;
        iter = memory_process["robot_hide"].begin();   
        memory_process["robot_hide"].insert(iter,robot_hide);
        memory_sum["robot_hide"] = memory_sum["robot_hide"] + robot_hide;
        iter = memory_process["human_hide"].begin();   
        memory_process["human_hide"].insert(iter,human_hide);
        memory_sum["human_hide"] = memory_sum["human_hide"] + human_hide;
        iter = memory_process["both_hide"].begin();   
        memory_process["both_hide"].insert(iter,both_hide);
        memory_sum["both_hide"] = memory_sum["both_hide"] + both_hide;
        
    }
    
    //calculate a score for the turn-taking synchronization between the
    //human and the robot for each form of turn-taking
    double drum_score = (0.5* (memory_sum["robot_drum"] + memory_sum["human_drum"]) \
                         - memory_sum["both_drum"])/(resolution*mem_length);
    //for peekaboo, if the robot or the person is hiding for too long
    //(over a couple of seconds), they are probably not being tracked
    // by the face-tracker, not hiding
    double hide_score = 0;
    if (memory_sum["robot_hide"] < (2.5*resolution))
        hide_score += memory_sum["robot_hide"];
    if (memory_sum["human_hide"] < (2.5*resolution))
        hide_score += memory_sum["human_hide"];
    hide_score = hide_score/(resolution*mem_length);
    
    //send the memory-based reward to motivation dynamics
    //not the cleanest way to implement, but avoids having to pass
    //whole memory to motivation dynamics
    outMem.addString("drum");    
    outMem.addDouble(drum_score);    
    outMem.addString("hide");    
    outMem.addDouble(hide_score);    
    
    outPort.write(outMem);
    
    return true;
}

bool ShortTermMemoryModule::respond(const Bottle &command,Bottle &reply){
        
    return false;
} 	
