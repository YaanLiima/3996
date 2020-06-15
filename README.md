- TFS 0.4 8.60
1. Compiled on Windows
	* x32 (dev-cpp): https://github.com/YaanLiima/binaries/raw/master/TheForgottenServer%20rev3996%208.6(x32).rar
	
2. Source: 
	* https://www.xtibia.com/forum/topic/249977-860-tfs-04-rev3996-war-cast/
	* https://tibiaking.com/forums/topic/96040-860-tfs-04-rev3996-war-cast/
	* https://github.com/YaanLiima/3996-master/

3. Dev-cpp: https://github.com/YaanLiima/binaries/raw/master/Stians%20Repack%20Dev-Cpp%200.2%2C%2064bit.rar

4. MSVC10 libs and includes: https://github.com/YaanLiima/binaries/raw/master/vc10_pack.rar
   MSVC15 libs and includes: https://github.com/YaanLiima/binaries/raw/master/vc15_pack.rar
   
5. Compile on linux:
	* apt-get install autogen autoconf build-essential pkg-config automake cmake g++ cpp gcc subversion lib32ncurses5-dev libreadline-dev libboost-dev libboost-thread-dev libboost-regex-dev libboost-date-time-dev libboost-filesystem-dev libgmp3-dev libxml2-dev liblua5.1-0-dev libssl-dev libmysql++-dev libtool libxml++-dev zlib1g-dev libcrypto++-dev libmysqlclient-dev
	* cd /3996-master
	* chmod -R 996 src
	* cd src
	* ./autogen.sh && ./configure --enable-sqlite --enable-mysql --enable-root-permission --enable-server-diag && ./build.sh
