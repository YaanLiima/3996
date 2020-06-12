- TFS 0.4 8.60
1. Compiled on Windows
	* x32 (dev-cpp): https://www.mediafire.com/file/ykyol6emdbjgi36/TheForgottenServer.rar/
	
2. Source: 
	* https://www.xtibia.com/forum/topic/249977-860-tfs-04-rev3996-war-cast/
	* https://tibiaking.com/forums/topic/96040-860-tfs-04-rev3996-war-cast/
	* https://github.com/YaanLiima/3996-master/

3. Dev-cpp: https://www.mediafire.com/file/jz3amjupi0aqm74/Stian%27s_Repack_Dev-Cpp_0.2%2C_64bit.rar/file

4. MSVC10 libs and includes: https://www.mediafire.com/file/giu6pe2kitum5rq/vc10_pack.rar/file

5. Compile on linux:
	* apt-get install autogen autoconf automake build-essential cmake g++ cpp gcc subversion lib32ncurses5-dev libreadline-dev libboost-dev libboost-thread-dev libboost-regex-dev libboost-date-time-dev libboost-filesystem-dev libxml2-dev libssl-dev libmysql++-dev libtool pkg-config libxml++-dev zlib1g-dev libcrypto++-dev libmysqlclient-dev
	* cd /3996-master
	* chmod -R 996 src
	* cd src
	* ./autogen.sh && ./configure --enable-sqlite --enable-mysql --enable-root-permission --enable-server-diag && ./build.sh
