/**
Software License Agreement (BSD)
\file      blackboard.h
\authors Xuefeng Chang <changxuefengcn@163.com>
\copyright Copyright (c) 2016, the micROS Team, HPCL (National University of Defense Technology), All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that
the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of micROS Team, HPCL, nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WAR-
RANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, IN-
DIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef BLACKBOARD_H_
#define BLACKBOARD_H_

#include <iostream>
#include <ros/ros.h>

#include "micros_swarm/message.h"
#include "micros_swarm/singleton.h"
#include "micros_swarm/runtime_handle.h"
#include "micros_swarm/comm_interface.h"

namespace micros_swarm{
    
    template<class Type>
    class BlackBoard{
        public:
            BlackBoard(){bb_id_=-1; on_robot_id_=-1; robot_id_=-1; is_local_=false;}

            BlackBoard(int bb_id, int on_robot_id)
            {
                bb_id_=bb_id;
                on_robot_id_=on_robot_id;
                rth_=Singleton<RuntimeHandle>::getSingleton();
                robot_id_=rth_->getRobotID();
                communicator_=Singleton<micros_swarm::CommInterface>::getExistedSingleton();
                is_local_=false;
                if(on_robot_id_==robot_id_)
                {
                    rth_->createBlackBoard(bb_id_);
                    is_local_=true;
                }
            }

            BlackBoard(const BlackBoard& bb)
            {
                rth_=Singleton<RuntimeHandle>::getSingleton();
                communicator_=Singleton<CommInterface>::getExistedSingleton();
                bb_id_=bb.bb_id_;
                on_robot_id_=bb.on_robot_id_;
                robot_id_=bb.robot_id_;
                is_local_=bb.is_local_;
            }

            BlackBoard& operator=(const BlackBoard& bb)
            {
                if(this==&bb)
                    return *this;
                rth_=Singleton<RuntimeHandle>::getSingleton();
                communicator_=Singleton<micros_swarm::CommInterface>::getExistedSingleton();
                bb_id_=bb.bb_id_;
                on_robot_id_=bb.on_robot_id_;
                robot_id_=bb.robot_id_;
                is_local_=bb.is_local_;
                return *this;
            }
            
            ~BlackBoard(){}
            
            void put(const std::string& key, const Type& data)
            {
                if(is_local_)
                {
                    std::ostringstream archiveStream;
                    boost::archive::text_oarchive archive(archiveStream);
                    archive<<data;
                    std::string s=archiveStream.str();
                    rth_->insertOrUpdateBlackBoard(bb_id_, key, s, time(0), rth_->getRobotID());
                }
                else
                {
                    std::ostringstream archiveStream;
                    boost::archive::text_oarchive archive(archiveStream);
                    archive<<data;
                    std::string s=archiveStream.str();
                    BlackBoardPut bbp(bb_id_,on_robot_id_, key, s, time(0), rth_->getRobotID());

                    std::ostringstream archiveStream2;
                    boost::archive::text_oarchive archive2(archiveStream2);
                    archive2<<bbp;
                    std::string bbp_str=archiveStream2.str();

                    micros_swarm::CommPacket p;
                    p.packet_source=rth_->getRobotID();
                    p.packet_version=1;
                    p.packet_type=BLACKBOARD_PUT;
                    p.packet_data=bbp_str;
                    p.package_check_sum=0;

                    rth_->getOutMsgQueue()->pushBbMsgQueue(p);
                }
            }
            
            Type get(const std::string& key)
            {
                if(is_local_)
                {
                    BlackBoardTuple bb;
                    rth_->getBlackBoardTuple(bb_id_, key, bb);

                    if (bb.bb_timestamp == 0) {
                        std::cout << "ID " << bb_id_ << " blackboard, " << key << " is not exist." << std::endl;
                        exit(-1);
                    }

                    std::string data_str = bb.bb_value;
                    Type data;
                    std::istringstream archiveStream(data_str);
                    boost::archive::text_iarchive archive(archiveStream);
                    archive >> data;
                    return data;
                }
                else
                {
                    BlackBoardQuery bbq(bb_id_, on_robot_id_, key, time(0), robot_id_);
                    std::ostringstream archiveStream2;
                    boost::archive::text_oarchive archive2(archiveStream2);
                    archive2 << bbq;
                    std::string bbq_str = archiveStream2.str();

                    micros_swarm::CommPacket p;
                    p.packet_source = rth_->getRobotID();
                    p.packet_version = 1;
                    p.packet_type = BLACKBOARD_QUERY;
                    p.packet_data=bbq_str;
                    p.package_check_sum = 0;
                    rth_->createBlackBoard(bb_id_);
                    rth_->insertOrUpdateBlackBoard(bb_id_, key, "", 0, -1);
                    rth_->getOutMsgQueue()->pushBbMsgQueue(p);

                    Type data;
                    BlackBoardTuple bbt;
                    ros::Rate loop_rate(100);
                    int count=0;
                    while(count<500)
                    {
                        rth_->getBlackBoardTuple(bb_id_, key, bbt);
                        if(bbt.bb_value!="")
                        {
                            std::string data_str=bbt.bb_value;
                            std::istringstream archiveStream(data_str);
                            boost::archive::text_iarchive archive(archiveStream);
                            archive>>data;
                            rth_->deleteBlackBoardValue(bb_id_, key);
                            break;
                        }
                        loop_rate.sleep();
                        ros::spinOnce();
                    }

                    return data;
                }
            }
            
            int size()
            {
                if(is_local_)
                {
                    return rth_->getBlackBoardSize(bb_id_);
                }
                else
                {

                }
            }
        private:
            int robot_id_;
            int bb_id_;
            int on_robot_id_;
            bool is_local_;
            boost::shared_ptr<RuntimeHandle> rth_;
            boost::shared_ptr<CommInterface> communicator_;
    };
}
#endif
