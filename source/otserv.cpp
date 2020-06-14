////////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
////////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////////////////////////////////////////////////////////////////////////
#include "otpch.h"
#include "otsystem.h"

#include <iostream>
#include <fstream>
#include <iomanip>

#ifndef WINDOWS
#include <sys/signal.h>
#include <unistd.h>
#include <termios.h>
#else
#include <conio.h>
#endif
#include <boost/config.hpp>

#include "server.h"
#ifdef __LOGIN_SERVER__
#include "gameservers.h"
#endif
#include "networkmessage.h"

#include "game.h"
#include "chat.h"
#include "tools.h"
#include "rsa.h"

#include "protocollogin.h"
#include "protocolgame.h"
#include "protocolold.h"
#include "protocolhttp.h"

#include "status.h"
#include "manager.h"
#ifdef __OTADMIN__
#include "admin.h"
#endif

#include "configmanager.h"
#include "scriptmanager.h"
#include "databasemanager.h"

#include "iologindata.h"
#include "ioban.h"

#include "outfit.h"
#include "vocation.h"
#include "group.h"

#include "monsters.h"
#ifdef __OTSERV_ALLOCATOR__
#include "allocator.h"
#endif
#ifdef __EXCEPTION_TRACER__
#include "exception.h"
#endif
#ifndef __OTADMIN__
#include "textlogger.h"
#endif

#ifdef __NO_BOOST_EXCEPTIONS__
#include <exception>

inline void boost::throw_exception(std::exception const & e)
{
	std::clog << "Boost exception: " << e.what() << std::endl;
}
#endif

RSA g_RSA;
ConfigManager g_config;
Game g_game;
Chat g_chat;
Monsters g_monsters;
Npcs g_npcs;

boost::mutex g_loaderLock;
boost::condition_variable g_loaderSignal;
boost::unique_lock<boost::mutex> g_loaderUniqueLock(g_loaderLock);
std::list<std::pair<uint32_t, uint32_t> > serverIps;

bool argumentsHandler(StringVec args)
{
	StringVec tmp;
	for(StringVec::iterator it = args.begin(); it != args.end(); ++it)
	{
		if((*it) == "--help")
		{
			std::clog << "Uso:\n"
			"\n"
			"\t--config=$1\t\tCaminho do arquivo de configuracao alternativo.\n"
			"\t--data-directory=$1\tCaminho do diretorio de dados alternativo.\n"
			"\t--ip=$1\t\t\tEndereco de IP do servidor.\n"
			"\t\t\t\tDeve ser igual ao IP global.\n"
			"\t--login-port=$1\tPorta do servidor de login on.\n"
			"\t--game-port=$1\tPorta do servidor de jogo on.\n"
			"\t--admin-port=$1\tPorta do admin on.\n"
			"\t--manager-port=$1\tPorta do manager server on.\n"
			"\t--status-port=$1\tPorta de status on.\n";
#ifndef WINDOWS
			std::clog << "\t--runfile=$1\t\tEspecifica arquivo executado. Ira conter o pid\n"
			"\t\t\t\tdo processo do servidor enquanto o estado de funcionamento.\n";
#endif
			std::clog << "\t--log=$1\t\tA Saida padrao inteira sera registrada no\n"
			"\t\t\t\teste ficheiro.\n"
			"\t--closed\t\t\tO servidor foi fechado.\n";
			return false;
		}

		if((*it) == "--version" || (*it) == "-V")
		{
			std::clog << SOFTWARE_NAME << ", Versao " << SOFTWARE_VERSION << " (" << SOFTWARE_CODENAME << ")\n"
			"Compilado com " << BOOST_COMPILER << " em " << __DATE__ << ", " << __TIME__ << ".\n"
			"TFS editada totalmente por Night nome real Yan Liima.\n"
			"Visite nosso forum para atualizacoes, suporte e recursos: xtibia.com ou tibiaking.com\n\n";
			return false;
		}

		tmp = explodeString((*it), "=");
		if(tmp[0] == "--config")
			g_config.setString(ConfigManager::CONFIG_FILE, tmp[1]);
		else if(tmp[0] == "--data-directory")
			g_config.setString(ConfigManager::DATA_DIRECTORY, tmp[1]);
		else if(tmp[0] == "--ip")
			g_config.setString(ConfigManager::IP, tmp[1]);
		else if(tmp[0] == "--login-port")
			g_config.setNumber(ConfigManager::LOGIN_PORT, atoi(tmp[1].c_str()));
		else if(tmp[0] == "--game-port")
			g_config.setNumber(ConfigManager::GAME_PORT, atoi(tmp[1].c_str()));
		else if(tmp[0] == "--admin-port")
			g_config.setNumber(ConfigManager::ADMIN_PORT, atoi(tmp[1].c_str()));
		else if(tmp[0] == "--manager-port")
			g_config.setNumber(ConfigManager::MANAGER_PORT, atoi(tmp[1].c_str()));
		else if(tmp[0] == "--status-port")
			g_config.setNumber(ConfigManager::STATUS_PORT, atoi(tmp[1].c_str()));
#ifndef WINDOWS
		else if(tmp[0] == "--runfile")
			g_config.setString(ConfigManager::RUNFILE, tmp[1]);
#endif
		else if(tmp[0] == "--log")
			g_config.setString(ConfigManager::OUTPUT_LOG, tmp[1]);
		else if(tmp[0] == "--closed")
			g_config.setBool(ConfigManager::START_CLOSED, true);
		else if(tmp[0] == "--no-script")
			g_config.setBool(ConfigManager::SCRIPT_SYSTEM, false);
	}

	return true;
}

#ifndef WINDOWS
int32_t getch()
{
	struct termios oldt;
	tcgetattr(STDIN_FILENO, &oldt);

	struct termios newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	int32_t ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	return ch;
}

void signalHandler(int32_t sig)
{
	switch(sig)
	{
		case SIGHUP:
			Dispatcher::getInstance().addTask(createTask(
				boost::bind(&Game::saveGameState, &g_game, false)));
			break;

		case SIGTRAP:
			g_game.cleanMap();
			break;

		case SIGCHLD:
			g_game.proceduralRefresh();
			break;

		case SIGUSR1:
			Dispatcher::getInstance().addTask(createTask(
				boost::bind(&Game::setGameState, &g_game, GAMESTATE_CLOSED)));
			break;

		case SIGUSR2:
			g_game.setGameState(GAMESTATE_NORMAL);
			break;

		case SIGCONT:
			Dispatcher::getInstance().addTask(createTask(
				boost::bind(&Game::reloadInfo, &g_game, RELOAD_ALL, 0)));
			break;

		case SIGQUIT:
			Dispatcher::getInstance().addTask(createTask(
				boost::bind(&Game::setGameState, &g_game, GAMESTATE_SHUTDOWN)));
			break;

		case SIGTERM:
			Dispatcher::getInstance().addTask(createTask(
				boost::bind(&Game::shutdown, &g_game)));
			break;

		default:
			break;
	}
}

void runfileHandler(void)
{
	std::ofstream runfile(g_config.getString(ConfigManager::RUNFILE).c_str(), std::ios::trunc | std::ios::out);
	runfile.close();
}
#else
int32_t getch()
{
	return (int32_t)getchar();
}
#endif

void allocationHandler()
{
	puts("Falha na alocacao, servidor com memoria insuficiente!\nDiminua o tamanho do seu mapa ou compila em um modo de 64 bits.");
	char buffer[1024];
	delete fgets(buffer, 1024, stdin);
	exit(-1);
}

void startupErrorMessage(std::string error = "")
{
	if(error.length() > 0)
		std::clog << std::endl << "> ERRO: " << error << std::endl;

	getch();
	exit(-1);
}

void otserv(StringVec args, ServiceManager* services);
int main(int argc, char* argv[])
{
	StringVec args = StringVec(argv, argv + argc);
	if(argc > 1 && !argumentsHandler(args))
		return 0;

	std::set_new_handler(allocationHandler);
	ServiceManager servicer;
	g_config.startup();

#ifdef __OTSERV_ALLOCATOR_STATS__
	boost::thread(boost::bind(&allocatorStatsThread, (void*)NULL));
	// TODO: shutdown this thread?
#endif
#ifdef __EXCEPTION_TRACER__
	ExceptionHandler mainExceptionHandler;
	mainExceptionHandler.InstallHandler();
#endif
#ifndef WINDOWS

	// ignore sigpipe...
	struct sigaction sigh;
	sigh.sa_handler = SIG_IGN;
	sigh.sa_flags = 0;

	sigemptyset(&sigh.sa_mask);
	sigaction(SIGPIPE, &sigh, NULL);

	// register signals
	signal(SIGHUP, signalHandler); //save
	signal(SIGTRAP, signalHandler); //clean
	signal(SIGCHLD, signalHandler); //refresh
	signal(SIGUSR1, signalHandler); //close server
	signal(SIGUSR2, signalHandler); //open server
	signal(SIGCONT, signalHandler); //reload all
	signal(SIGQUIT, signalHandler); //save & shutdown
	signal(SIGTERM, signalHandler); //shutdown
#endif

	OutputHandler::getInstance();
	Dispatcher::getInstance().addTask(createTask(boost::bind(otserv, args, &servicer)));

	g_loaderSignal.wait(g_loaderUniqueLock);
	boost::this_thread::sleep(boost::posix_time::milliseconds(10000));
	if(servicer.isRunning())
	{
		std::clog << ">> " << g_config.getString(ConfigManager::SERVER_NAME) << " servidor Online!" << std::endl << std::endl;
		servicer.run();
	}
	else
		std::clog << ">> " << g_config.getString(ConfigManager::SERVER_NAME) << " servidor Offline! Nao ha servicos disponiveis..." << std::endl << std::endl;

#ifdef __EXCEPTION_TRACER__
	mainExceptionHandler.RemoveHandler();
#endif
	return 0;
}

void otserv(StringVec, ServiceManager* services)
{
	srand((uint32_t)OTSYS_TIME());
#if defined(WINDOWS)
	SetConsoleTitle(SOFTWARE_NAME);

#endif
	g_game.setGameState(GAMESTATE_STARTUP);
#if !defined(WINDOWS) && !defined(__ROOT_PERMISSION__)
	if(!getuid() || !geteuid())
	{
		std::clog << "> AVISO: " << SOFTWARE_NAME << " foi executado como superusuario! isto e "
			<< "recomendado para ser executado como um usuario normal." << std::endl << "Continuar? (y/N)" << std::endl;
		char buffer = getch();
		if(buffer != 121 && buffer != 89)
			startupErrorMessage("Abortado.");
	}
#endif

	std::clog << SOFTWARE_NAME << ", Versao " << SOFTWARE_VERSION << " (" << SOFTWARE_CODENAME << ")\n"
		"Compilado com " << BOOST_COMPILER << " em " << __DATE__ << ", " << __TIME__ << ".\n"
		"TFS editada totalmente por Night nome real Yan Liima.\n"
		"Visite nosso forum para atualizacoes, suporte e recursos: xtibia.com ou tibiaking.com\n\n";

	std::stringstream ss;
#ifdef __DEBUG__
	ss << " GLOBAL";
#endif
#ifdef __DEBUG_MOVESYS__
	ss << " MOVESYS";
#endif
#ifdef __DEBUG_CHAT__
	ss << " CHAT";
#endif
#ifdef __DEBUG_EXCEPTION_REPORT__
	ss << " EXCEPTION-REPORT";
#endif
#ifdef __DEBUG_HOUSES__
	ss << " HOUSES";
#endif
#ifdef __DEBUG_LUASCRIPTS__
	ss << " LUA-SCRIPTS";
#endif
#ifdef __DEBUG_MAILBOX__
	ss << " MAILBOX";
#endif
#ifdef __DEBUG_NET__
	ss << " NET";
#endif
#ifdef __DEBUG_NET_DETAIL__
	ss << " NET-DETAIL";
#endif
#ifdef __DEBUG_RAID__
	ss << " RAIDS";
#endif
#ifdef __DEBUG_SCHEDULER__
	ss << " SCHEDULER";
#endif
#ifdef __DEBUG_SPAWN__
	ss << " SPAWNS";
#endif
#ifdef __SQL_QUERY_DEBUG__
	ss << " SQL-QUERIES";
#endif

	std::string debug = ss.str();
	if(!debug.empty())
		std::clog << ">> Depuracao:" << debug << "." << std::endl;

	std::clog << ">> Carregando Configuracoes (" << g_config.getString(ConfigManager::CONFIG_FILE) << ")" << std::endl;
	if(!g_config.load())
		startupErrorMessage("Incapaz de carregar " + g_config.getString(ConfigManager::CONFIG_FILE) + "!");

	// silently append trailing slash
	std::string path = g_config.getString(ConfigManager::DATA_DIRECTORY);
	g_config.setString(ConfigManager::DATA_DIRECTORY, path.erase(path.find_last_not_of("/") + 1) + "/");

	path = g_config.getString(ConfigManager::LOGS_DIRECTORY);
	g_config.setString(ConfigManager::LOGS_DIRECTORY, path.erase(path.find_last_not_of("/") + 1) + "/");

	std::clog << "> Abrindo Historico" << std::endl;
	Logger::getInstance()->open();

	IntegerVec cores = vectorAtoi(explodeString(g_config.getString(ConfigManager::CORES_USED), ","));
	if(cores[0] != -1)
	{
#ifdef WINDOWS
		int32_t mask = 0;
		for(IntegerVec::iterator it = cores.begin(); it != cores.end(); ++it)
			mask += 1 << (*it);

		SetProcessAffinityMask(GetCurrentProcess(), mask);
	}

	std::stringstream mutexName;
	mutexName << "forgottenserver_" << g_config.getNumber(ConfigManager::WORLD_ID);

	CreateMutex(NULL, FALSE, mutexName.str().c_str());
	if(GetLastError() == ERROR_ALREADY_EXISTS)
		startupErrorMessage("Outra distro do servidor ja esta sendo executada.\nSe voce quiser executar multiplos servidores, por favor mudar o mundo no arquivo de configuracao.");

	std::string defaultPriority = asLowerCaseString(g_config.getString(ConfigManager::DEFAULT_PRIORITY));
	if(defaultPriority == "realtime")
		SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
	else if(defaultPriority == "high")
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	else if(defaultPriority == "higher")
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

#else
#ifndef MACOS
		cpu_set_t mask;
		CPU_ZERO(&mask);
		for(IntegerVec::iterator it = cores.begin(); it != cores.end(); ++it)
			CPU_SET((*it), &mask);

		sched_setaffinity(getpid(), (int32_t)sizeof(mask), &mask);
	}
#endif

	std::string runPath = g_config.getString(ConfigManager::RUNFILE);
	if(runPath != "" && runPath.length() > 2)
	{
		std::ofstream runFile(runPath.c_str(), std::ios::trunc | std::ios::out);
		runFile << getpid();
		runFile.close();
		atexit(runfileHandler);
	}

	if(!nice(g_config.getNumber(ConfigManager::NICE_LEVEL))) {}
#endif
	std::string encryptionType = asLowerCaseString(g_config.getString(ConfigManager::ENCRYPTION_TYPE));
	if(encryptionType == "md5")
	{
		g_config.setNumber(ConfigManager::ENCRYPTION, ENCRYPTION_MD5);
		std::clog << "> Usando MD5 criptografia" << std::endl;
	}
	else if(encryptionType == "sha1")
	{
		g_config.setNumber(ConfigManager::ENCRYPTION, ENCRYPTION_SHA1);
		std::clog << "> Usando SHA1 criptografia" << std::endl;
	}
	else if(encryptionType == "sha256")
	{
		g_config.setNumber(ConfigManager::ENCRYPTION, ENCRYPTION_SHA256);
		std::clog << "> Usando SHA256 criptografia" << std::endl;
	}
	else if(encryptionType == "sha512")
	{
		g_config.setNumber(ConfigManager::ENCRYPTION, ENCRYPTION_SHA512);
		std::clog << "> Usando SHA512 criptografia" << std::endl;
	}
	else if(encryptionType == "vahash")
	{
		g_config.setNumber(ConfigManager::ENCRYPTION, ENCRYPTION_VAHASH);
		std::clog << "> Usando VAHash criptografia" << std::endl;
	}
	else
	{
		g_config.setNumber(ConfigManager::ENCRYPTION, ENCRYPTION_PLAIN);
		std::clog << "> Usando PlainText criptografia" << std::endl;
	}

	/* std::clog << ">> Checando a versao do Software... ";
	if(xmlDocPtr doc = xmlParseFile(VERSION_CHECK))
	{
		xmlNodePtr p, root = xmlDocGetRootElement(doc);
		if(!xmlStrcmp(root->name, (const xmlChar*)"versions"))
		{
			p = root->children->next;
			if(!xmlStrcmp(p->name, (const xmlChar*)"entry"))
			{
				std::string version;
				int32_t patch, build, timestamp;

				bool tmp = false;
				if(readXMLString(p, "version", version) && version != SOFTWARE_VERSION)
					tmp = true;

				if(readXMLInteger(p, "patch", patch) && patch > VERSION_PATCH)
					tmp = true;

				if(readXMLInteger(p, "build", build) && build > VERSION_BUILD)
					tmp = true;

				if(readXMLInteger(p, "timestamp", timestamp) && timestamp > VERSION_TIMESTAMP)
					tmp = true;

				if(tmp)
				{
					std::clog << " ";
					if(version.find("_SVN") == std::string::npos)
						std::clog << "executando sub versao, entre em mente e instavel e apenas para fins de teste!";
					else
						std::clog << "desatualizado, por favor, considere a atualizacao!";

					std::clog << std::endl << "> Informacoes sobre a versao atual - versao: "
						<< SOFTWARE_VERSION << ", patch: " << VERSION_PATCH
						<< ", build: " << VERSION_BUILD << ", timestamp: " << VERSION_TIMESTAMP
						<< "." << std::endl << "> Informacoes versao mais recente - versao: "
						<< version << ", patch: " << patch << ", build: " << build
						<< ", timestamp: " << timestamp << "." << std::endl;
					if(g_config.getBool(ConfigManager::CONFIRM_OUTDATED_VERSION) &&
						asLowerCaseString(version).find("_svn") == std::string::npos)
					{
						std::clog << "Continuar? (y/N)" << std::endl;
						char buffer = getch();
						if(buffer != 121 && buffer != 89)
							startupErrorMessage("Abortado.");
					}
				}
				else
					std::clog << "atualizado!" << std::endl;
			}
			else
				std::clog << "falhou a verificacao - entrada malformado." << std::endl;
		}
		else
			std::clog << "verificacao falhou - arquivo malformado." << std::endl;

		xmlFreeDoc(doc);
	}
	else
		std::clog << "falhou - nao foi possivel analisar arquivo remoto (voce esta conectado em alguma rede?)" << std::endl; */

	std::clog << ">> Carregando RSA key" << std::endl;
	// This check will verify keys set in config.lua
	const char* p(g_config.getString(ConfigManager::RSA_PRIME1).c_str());
	const char* q(g_config.getString(ConfigManager::RSA_PRIME2).c_str());
	const char* d(g_config.getString(ConfigManager::RSA_PRIVATE).c_str());
	
    g_RSA.initialize(p, q, d);
	std::clog << ">> Iniciando conexao SQL" << std::endl;
	Database* db = Database::getInstance();
	if(db && db->isConnected())
	{
		std::clog << ">> Executando Gerenciador de Banco de Dados" << std::endl;
		if(DatabaseManager::getInstance()->isDatabaseSetup())
		{
			uint32_t version = 0;
			do
			{
				version = DatabaseManager::getInstance()->updateDatabase();
				if(!version)
					break;

				std::clog << "> Banco de dados foi atualizado para a versao: " << version << "." << std::endl;
			}
			while(version < VERSION_DATABASE);
		}
		else
			startupErrorMessage("O banco de dados que especificou no config.lua esta vazia, por favor importar esquemas/<engine>.sql para a base de dados.");

		DatabaseManager::getInstance()->checkTriggers();
		DatabaseManager::getInstance()->checkEncryption();
		if(g_config.getBool(ConfigManager::OPTIMIZE_DATABASE) && !DatabaseManager::getInstance()->optimizeTables())
			std::clog << "> Sem tabelas para optimizar." << std::endl;
	}
	else
		startupErrorMessage("Nao foi possivel estabelecer conexao com banco de dados SQL!");

	std::clog << ">> Carregando items (OTB)" << std::endl;
	if(Item::items.loadFromOtb(getFilePath(FILE_TYPE_OTHER, "items/items.otb")))
		startupErrorMessage("Nao foi possivel carregar items (OTB)!");

	std::clog << ">> Carregando items (XML)" << std::endl;
	if(!Item::items.loadFromXml())
	{
		std::clog << "Nao foi possivel carregar items (XML)! Continuar? (y/N)" << std::endl;
		char buffer = getch();
		if(buffer != 121 && buffer != 89)
			startupErrorMessage("Nao foi possivel carregar items (XML)!");
	}

	std::clog << ">> Carregando Grupos" << std::endl;
	if(!Groups::getInstance()->loadFromXml())
		startupErrorMessage("Nao foi possivel carregar groups!");

	std::clog << ">> Carregando Vocacoes" << std::endl;
	if(!Vocations::getInstance()->loadFromXml())
		startupErrorMessage("Nao foi possivel carregar vocations!");

	std::clog << ">> Carregando Outfits" << std::endl;
	if(!Outfits::getInstance()->loadFromXml())
		startupErrorMessage("Nao foi possivel carregar outfits!");

	std::clog << ">> Carregando Canal de Chat" << std::endl;
	if(!g_chat.loadFromXml())
		startupErrorMessage("Nao foi possivel carregar chat channels!");

	if(g_config.getBool(ConfigManager::SCRIPT_SYSTEM))
	{
		std::clog << ">> Carregando Sistema" << std::endl;
		if(!ScriptManager::getInstance()->loadSystem())
			startupErrorMessage();
	}
	else
		ScriptManager::getInstance();

	std::clog << ">> Carregando Mods..." << std::endl;
	if(!ScriptManager::getInstance()->loadMods())
		startupErrorMessage();

	#ifdef __LOGIN_SERVER__
	std::clog << ">> Carregando game servers" << std::endl;
	if(!GameServers::getInstance()->loadFromXml(true))
		startupErrorMessage("Nao foi possivel carregar game servers!");

	#endif
	std::clog << ">> Carregando EXP Stages" << std::endl;
	if(!g_game.loadExperienceStages())
		startupErrorMessage("Nao foi possivel carregar experience stages!");

	std::clog << ">> Carregando Monstros" << std::endl;
	if(!g_monsters.loadFromXml())
	{
		std::clog << "Nao foi possivel carregar monsters! Continuar? (y/N)" << std::endl;
		char buffer = getch();
		if(buffer != 121 && buffer != 89)
			startupErrorMessage("Nao foi possivel carregar monsters!");
	}

	std::clog << ">> Carregando Mapa e Spawns..." << std::endl;
	if(!g_game.loadMap(g_config.getString(ConfigManager::MAP_NAME)))
		startupErrorMessage();

	std::clog << "> Carregando Tipo de PVP... ";
	std::string worldType = asLowerCaseString(g_config.getString(ConfigManager::WORLD_TYPE));
	if(worldType == "open" || worldType == "2" || worldType == "openpvp")
	{
		g_game.setWorldType(WORLDTYPE_OPEN);
		std::clog << "Open PvP" << std::endl;
	}
	else if(worldType == "optional" || worldType == "1" || worldType == "optionalpvp")
	{
		g_game.setWorldType(WORLDTYPE_OPTIONAL);
		std::clog << "Optional PvP" << std::endl;
	}
	else if(worldType == "hardcore" || worldType == "3" || worldType == "hardcorepvp")
	{
		g_game.setWorldType(WORLDTYPE_HARDCORE);
		std::clog << "Hardcore PvP" << std::endl;
	}
	else
	{
		std::clog << std::endl;
		startupErrorMessage("Tipo de PVP desconhecido: " + g_config.getString(ConfigManager::WORLD_TYPE));
	}

	std::clog << "> Inicializando estado do jogo e registrando servicos..." << std::endl;
	g_game.setGameState(GAMESTATE_INIT);
	IPAddressList ipList;

	std::string ip = g_config.getString(ConfigManager::IP);
	if(asLowerCaseString(ip) == "auto")
	{
		// TODO: automatic shit
	}

	IPAddress m_ip;
	if(ip.size())
	{
		std::clog << "> IP do Servidor: ";
		uint32_t resolvedIp = inet_addr(ip.c_str());
		if(resolvedIp == INADDR_NONE)
		{
			struct hostent* host = gethostbyname(ip.c_str());
			if(!host)
			{
				std::clog << "..." << std::endl;
				startupErrorMessage("Nao e possivel resolver " + ip + "!");
			}

			resolvedIp = *(uint32_t*)host->h_addr;
		}

		serverIps.push_front(std::make_pair(resolvedIp, 0));
		m_ip = boost::asio::ip::address_v4(swap_uint32(resolvedIp));

		ipList.push_back(m_ip);
		std::clog << m_ip.to_string() << std::endl;
	}

	ipList.push_back(boost::asio::ip::address_v4(INADDR_LOOPBACK));
	if(!g_config.getBool(ConfigManager::BIND_ONLY_GLOBAL_ADDRESS))
	{
		char hostName[128];
		if(!gethostname(hostName, 128))
		{
			if(hostent* host = gethostbyname(hostName))
			{
				std::stringstream s;
				for(uint8_t** addr = (uint8_t**)host->h_addr_list; addr[0] != NULL; addr++)
				{
					uint32_t resolved = swap_uint32(*(uint32_t*)(*addr));
					if(m_ip.to_v4().to_ulong() == resolved)
						continue;

					ipList.push_back(boost::asio::ip::address_v4(resolved));
					serverIps.push_front(std::make_pair(*(uint32_t*)(*addr), 0x0000FFFF));

					s << (int32_t)(addr[0][0]) << "." << (int32_t)(addr[0][1]) << "."
						<< (int32_t)(addr[0][2]) << "." << (int32_t)(addr[0][3]) << "\t";
				}

				if(s.str().size())
					std::clog << "> IP local do Servidor: " << s.str() << std::endl;
			}
		}

		serverIps.push_front(std::make_pair(LOCALHOST, 0xFFFFFFFF));
		if(m_ip.to_v4().to_ulong() != LOCALHOST)
			ipList.push_back(boost::asio::ip::address_v4(LOCALHOST));
	}
	else if(ipList.size() < 2)
		startupErrorMessage("Nao e possivel vincular quaisquer endereco de IP! Voce pode desativar \"bindOnlyGlobalAddress\" em config.lua");

	services->add<ProtocolStatus>(g_config.getNumber(ConfigManager::STATUS_PORT), ipList);
	services->add<ProtocolManager>(g_config.getNumber(ConfigManager::MANAGER_PORT), ipList);
	#ifdef __OTADMIN__
	services->add<ProtocolAdmin>(g_config.getNumber(ConfigManager::ADMIN_PORT), ipList);
	#endif

	//services->add<ProtocolHTTP>(8080, ipList);
	if(
#ifdef __LOGIN_SERVER__
	true
#else
	!g_config.getBool(ConfigManager::LOGIN_ONLY_LOGINSERVER)
#endif
	)
	{
		services->add<ProtocolLogin>(g_config.getNumber(ConfigManager::LOGIN_PORT), ipList);
		services->add<ProtocolOldLogin>(g_config.getNumber(ConfigManager::LOGIN_PORT), ipList);
	}

	services->add<ProtocolGame>(g_config.getNumber(ConfigManager::GAME_PORT), ipList);
	services->add<ProtocolOldGame>(g_config.getNumber(ConfigManager::LOGIN_PORT), ipList);
	std::clog << "> Portas do Servidor: ";

	std::list<uint16_t> ports = services->getPorts();
	for(std::list<uint16_t>::iterator it = ports.begin(); it != ports.end(); ++it)
		std::clog << (*it) << "\t";

	std::clog << std::endl << ">> Tudo ocorreu bem ate aqui, o servidor sera iniciado..." << std::endl;
	g_game.start(services);
	g_game.setGameState(g_config.getBool(ConfigManager::START_CLOSED) ? GAMESTATE_CLOSED : GAMESTATE_NORMAL);
	g_loaderSignal.notify_all();
}
