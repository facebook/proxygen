FROM ubuntu                                                                                                                                                   
MAINTAINER Riza Alaudin Syah version: 0.1 version: 0.1                                                                                                        
                                                                                                                                                              
RUN apt-get update                                                                                                                                            
RUN apt-get install -y git                                                                                                                                    
RUN apt-get install -y curl                                                                                                                                   
RUN apt-get install -y \                                                                                                                                      
    flex \                                                                                                                                                    
    bison \                                                                                                                                                   
    libkrb5-dev \                                                                                                                                             
    libsasl2-dev \                                                                                                                                            
    libnuma-dev \                                                                                                                                             
    pkg-config \                                                                                                                                              
    libssl-dev \                                                                                                                                              
    libcap-dev \                                                                                                                                              
    ruby \                                                                                                                                                    
    gperf \                                                                                                                                                   
    autoconf-archive \                                                                                                                                        
    libevent-dev \                                                                                                                                            
    libgoogle-glog-dev \                                                                                                                                      
    wget                                                                                                                                                      
WORKDIR /home                                                                                                                                                 
RUN git clone https://github.com/facebook/proxygen.git                                                                                                        
WORKDIR /home/proxygen/proxygen                                                                                                                               
RUN ./deps.sh
