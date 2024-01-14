// tmon: a tiny system monitor for linux
// pondda@protonmail.com
// 2024
// GNU General Public License, version 3 (GPL-3.0)

#include <ncurses.h>
#include <string>
#include <cstring>
// #include <format> // C++ 20+ only
#include <locale>
#include <iostream>
#include <ctime>
#include <math.h>
#include <algorithm>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <filesystem>
#include <chrono>
namespace fs = std::filesystem;
using Meminfo = std::unordered_map<std::string, float>;

#define BUF 256

// CONFIG -------------------------------------------------------
struct Config{
	int interval;
	std::string temp_sensor;
	int temp_min;
	int temp_max;
};

const char* confStr =
	"interval 2000\n"
	"temp_sensor \"Core 0\"\n"
	"temp_min 0\n"
	"temp_max 100";

void generateConf(){
	if (const char* home = std::getenv("HOME")){
		std::string path = home;
		path += "/.config/tmon.conf";
		std::cout << "Generating config file at " << path << std::endl;
		std::ofstream f(path);
		if (f.fail()){
			std::cout << "Could not create config file at" << path << std::endl;
			exit(1);
		}
		f << confStr << std::endl;
		f.close();
	} else {
		std::cout << "$HOME not found. Failed to generate config file" << std::endl;
		exit(1);
	}
}

std::string parseStr(std::string line){
	std::string::difference_type n = std::count(line.begin(), line.end(), '\"');
	if (n != 2) {
		std::cout << "Invalid formatting in config file" << std::endl;
		exit(1);
	}

	std::string::size_type start = 0;
	std::string::size_type end = 0;

	start = line.find("\"");
	++start;
	end = line.find("\"");
	return line.substr(start - 1, end - start);
}

Config parseConfig(){
	Config conf;
	if (const char* home = std::getenv("HOME")){
		std::string path = home;
		path += "/.config/tmon.conf";
		std::ifstream f(path);
		if (f.fail()){
			generateConf();
			f.open(path);
		}
		for (std::string line; std::getline(f, line);){
			std::istringstream ss(line);
			std::string key;
			ss >> key;
			if (key == "interval") ss >> conf.interval;
			else if (key == "temp_sensor") conf.temp_sensor = parseStr(line);
			else if (key == "temp_min") ss >> conf.temp_min;
			else if (key == "temp_max") ss >> conf.temp_max;
		}

	} else {
		std::cout << "$HOME not found. Could not load config file" << std::endl;
		exit(1);
	}
	return conf;
}

// utils ---------------------------------------
std::string getCmdOut(std::string cmdstr){
	FILE *f;
	const char *cmd = cmdstr.c_str();
	if ((f = popen(cmd, "r")) == NULL){
		perror("popen failed");
		exit(EXIT_FAILURE);
	}
	
	char buf[BUF];
	std::string result;

	while (fgets(buf, sizeof(buf), f)){
		result += buf;
	}
	fclose(f);
	if (result != "") result.pop_back();
	return result;
}

float s2f(std::string s){
	float f;
	try {
		f = stof(s);
	}
	catch (std::invalid_argument& e){
		f = 0.0;
	}
	return f;
}

int b2i(bool b){
	return (static_cast<int>(b) * 2) -1;
}

float kbToGb(float kb){
	return kb/1048576.0;
}

float roundToDp(float f, int dp){
	float e = pow(10, dp);
	f *= e;
	f = static_cast<float>(round(f));
	return f / e;
}

std::string pad(std::string s, int n){
	for (int i=0; i < n-s.size(); i++) s += " ";
	return s;
}

// PROGRESS BARS -----------------------------------------------------
std::string progBarGui(float prog, int nb){
	std::string result;

	float block = 1.0/float(nb);
	for (int i=0; i<nb; i++){
		std::string s;
		float p = prog - i*block;
		if (p < 0) s = " ";
		else if((i+1)*block < prog) s = "█";
		else {
			int ascii = static_cast<int>(round((p/block) * 8.0));
			switch (ascii) {
				case 0:
					s = " ";
					break;
				case 1:
					s = "▏";
					break;
				case 2:
					s = "▎";
					break;
				case 3:
					s = "▍";
					break;
				case 4:
					s = "▌";
					break;
				case 5:
					s = "▋";
					break;
				case 6:
					s = "▊";
					break;
				case 7:
					s = "▉";
					break;
				case 8:
					s = "█";
					break;
			}
		}
		result += s;
	}
	return result;
}

std::string progBarTty(float prog, int nb){
	std::string result;
	float block = 1.0/float(nb);
	for (int i=0; i<nb; i++){
		if (prog < float(i)*block + block/2){
			result += " ";	
		} else result += "█";
	}
	return result;
}

// DATE & TIME---------------------------------------------------
std::string getDateTime(bool gui){
	std::string result;
	if (gui) result += "🕒 ";

	std::time_t t = std::time(nullptr);
	char str[17];
	if (std::strftime(str, sizeof(str), "%Y-%m-%d %H:%M", std::localtime(&t))) result += str;

	return result;
}

// BATTERY ------------------------------------------------------
bool batCheck(std::string &batdir){
	for (const auto &dir: fs::directory_iterator("/sys/class/power_supply/")){
		std::string dirstr = dir.path().string();
		std::ifstream f(dirstr + "/type");
		std::string s;
		f >> s;
		if (s == "Battery"){
			batdir = dirstr;
			return true;
		}
	}
	return false;
}

struct Battinfo {
	std::string state;
	int capacity;
	int time;
};

Battinfo getBattinfo(std::string batdir){
	Battinfo info;

	std::ifstream f(batdir + "/status");
	f >> info.state;
	f.close();

	f.open(batdir + "/capacity");
	f >> info.capacity;
	f.close();

	float full, curr, rate;
	f.open(batdir + "/charge_full");
	if (f.fail()) f.open(batdir + "/energy_full");
	f >> full;
	f.close();

	f.open(batdir + "/charge_now");
	if (f.fail()) f.open(batdir + "/energy_now");
	f >> curr;
	f.close();

	f.open(batdir + "/current_now");
	if (f.fail()) f.open(batdir + "/power_now");
	f >> rate;
	f.close();

	// set the remaining time in seconds
	full /= 1000;
	curr /= 1000;
	rate /= 1000;
	if (info.state == "Charging") info.time = ((full - curr) / rate)* 3600;
	else if (info.state == "Discharging") info.time =  (curr / rate) * 3600;
	else info.time = 0;

	return info;
}

std::string getTimeStr(int seconds){
	if (seconds <= 0.0) return "";

	int hours, minutes;
	hours = seconds / 3600;
	seconds -= hours * 3600;
	minutes = seconds / 60;
	
	std::string h, m;
	h = std::to_string(hours);
	m = std::to_string(minutes);
	if (h.size() < 2) h = "0" + h;
	if (m.size() < 2) m = "0" + m;
	
	return h + ":" + m;
}

std::string getIcon(std::string state){
	if (state ==  "Discharging") return "⚡";
	return "🔌";
}

std::string getBat(bool gui, std::string batdir){
	Battinfo info = getBattinfo(batdir);
	std::string result;

	if (gui) result += getIcon(info.state) + " ";
	std::string s = std::to_string(info.capacity) + "%";
	result += pad(s, 4);
	float b = static_cast<float>(info.capacity)/100.0;

	result += "║";
	if (gui) result += progBarGui(b, 4);
	else result += progBarTty(b, 4);
	result += "╠ ";
	result += getTimeStr(info.time);
	return result;
}

// LOAD/CPU ------------------------------------------------------------------------------
struct Cpuinfo {
	unsigned long long user;
	unsigned long long nice;
	unsigned long long sys;
	unsigned long long idle;
	unsigned long long iowait;
	unsigned long long irq;
	unsigned long long softirq;
	unsigned long long steal;
	unsigned long long guest;
	unsigned long long guest_nice;
};

Cpuinfo getCpuinfo(){
	Cpuinfo info;
	std::ifstream f("/proc/stat");
	std::string s;
	f >> s >> info.user \
		>> info.nice \
		>> info.sys \
		>> info.idle \
		>> info.iowait \
		>> info.irq \
		>> info.softirq \
		>> info.steal \
		>> info.guest \
		>> info.guest_nice;
	return info;
}

unsigned long long getCpuTotal(Cpuinfo &info){
	unsigned long long total = 0;
	total += info.user;
	total += info.nice;
	total += info.sys;
	total += info.idle;
	total += info.iowait;
	total += info.irq;
	total += info.softirq;
	total += info.steal;
	total += info.guest;
	total += info.guest_nice;
	return total;
}

void setCpu(std::atomic<float> &cpu, std::atomic<bool> &bRun){
	unsigned long long currTotal, currUsed;
	unsigned long long prevTotal = 0;
	unsigned long long prevUsed = 0;
	unsigned long long total, used;
	Cpuinfo info;

	while (bRun){
		info = getCpuinfo();
		currTotal = getCpuTotal(info);
		currUsed = currTotal - info.idle;

		total = currTotal - prevTotal;
		used = currUsed - prevUsed;
		cpu = static_cast<float>( static_cast<double>(used) / static_cast<double>(total) );

		prevTotal = currTotal;
		prevUsed = currUsed;

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}

std::string getLoad(){
	std::string load;
	std::ifstream f("/proc/loadavg");
	f >> load;
	return load;
}

std::string getCpu(bool gui, float cpu){
	std::string load = getLoad();
	std::string result;
	if (gui) result += "🖥  ";
	result += pad(load, 9);
	// result += std::format("{:<7}", load); // C++ 20+ only

	result += "[";
	if (gui) result += progBarGui(cpu, 7);
	else result += progBarTty(cpu, 7);
	result += "]";
	return result;
}

// MEMORY -----------------------------------------------------------
Meminfo getMeminfo(){
	Meminfo meminfo;
	std::ifstream f("/proc/meminfo");
	for (std::string line; std::getline(f, line);){
		std::istringstream ss(line);
		std::string key;
		float value;
		ss >> key >> value;
		if (!key.empty()) key.pop_back(); // remove the trailing ":"
		meminfo[key] = value;
	}
	return meminfo;
}

std::string getMem(bool gui){
	Meminfo meminfo = getMeminfo();

	std::string result;
	if (gui) result += "🎟  ";
	float total = meminfo["MemTotal"];
	float used = total - meminfo["MemFree"];
	used -= meminfo["Buffers"] + meminfo["Cached"];
	int dp = 1; // decimal place to round to
	float usedGb = roundToDp(kbToGb(used), dp);
	std::string s = std::to_string(usedGb);
	s = s.substr(0, s.find(".")+dp+1) + "GB";
	result += pad(s, 8);
	// result += std::format("{:<7}", s); // C++ 20+ only

	used /= total;
	result += "[";
	if (gui) result += progBarGui(used, 7);
	else result += progBarTty(used, 7);
	result += "]";
	return result;
}

// TEMPERATURE---------------------------------------------------
std::string getTemp(bool gui, Config &conf){
	std::string cmd = "sensors | grep " + conf.temp_sensor + " | grep -o \"[0-9]*.[0-9]°C\" | head -1";
	std::string s = getCmdOut(cmd);
	
	std::string result;
	if (gui) result += "🌡️  ";
	result += pad(s, 8);
	// result += std::format("{:<8}", s); // C++ 20+ only
	
	s = s.substr(0, s.find("°C"));
	float temp = s2f(s);
	temp = (temp - conf.temp_min)  / (conf.temp_max - conf.temp_min);
	result += "[";
	if (gui) result += progBarGui(temp, 7);
	else result += progBarTty(temp, 7);
	result += "]";
	return result;
}

// HELP --------------------------------------------------------------------
const char* helpStr =
	"tmon\n"
	"a tiny system monitor for Linux\n"
	"\n"
	"Usage: tmon [OPTIONS]\n"
	"\n"
	"Options:\n"
	"--help, -h	Prints this help and exits\n"
	"\n"
	"Keybinds:\n"
	"Q:	Exit\n"
	"H:	Toggle this help\n"
	"Space:	Toggle unicode mode (default on)\n"
	"D:	Toggle date and time\n"
	"B:	Toggle battery\n"
	"C:	Toggle load and CPU utilisation\n"
	"M:	Toggle memory usage\n"
	"T:	Toggle CPU temperature\n";

void printHelp(){
	printf(helpStr);
}

void parseArgs(int argc, char* argv[]){
	for (int i=1; i<argc; i++){
		char* str = argv[i];
		if (strcmp(str, "-h") == 0 || strcmp(str, "--help") == 0){	
			printHelp();
			exit(0);
		}
	}
}

// MAIN ----------------------------------------------------------------------
int main(int argc, char* argv[]){
	parseArgs(argc, argv);

	Config conf = parseConfig();

	setlocale(LC_ALL, ""); // so we can use emojis!
	initscr();
	noecho();
	curs_set(0);
	cbreak();

	bool gui = true;
	bool bDateTime = true;
	bool bBatt = true;
	bool bLoad = true;
	bool bMem = true;
	bool bTemp = true;
	bool bHelp = false;

	float w = 19; //static_cast<float>(getDateTime().size());
	int scrx, scry;
	int nLines = 3; // 5 minus bat and temp
	
	std::string batdir;
	bool batSafe = batCheck(batdir);
	bool tempSafe = system("sensors 1>/dev/null") == 0;
	nLines += batSafe + tempSafe;

	// run the cpu utilisation function in a separate thread
	std::atomic<bool> bRun = true;
	std::atomic<float> cpu = 0.0;
	std::thread cpuThr(setCpu, std::ref(cpu), std::ref(bRun));
	cpuThr.detach();

	while (bRun){
		erase();

		getmaxyx(stdscr, scry, scrx);
		float x = (scrx - w) / 2.0;
		float y = (scry - nLines) / 2.0;
		int offset = 0;

		if (bDateTime)		{ mvprintw(y+offset, x, getDateTime(gui).c_str()); offset++; }
		if (bBatt && batSafe)	{ mvprintw(y+offset, x, getBat(gui, batdir).c_str()); offset++; }
		if (bLoad)		{ mvprintw(y+offset, x, getCpu(gui, cpu).c_str()); offset++; }
		if (bMem)		{ mvprintw(y+offset, x, getMem(gui).c_str()); offset++; }
		if (bTemp && tempSafe)	{ mvprintw(y+offset, x, getTemp(gui, conf).c_str()); offset++; }
		if (bHelp)		mvprintw(0, 0, helpStr);

		refresh();

		timeout(conf.interval);
		char key = getch();
		switch (key) {
			case 'q':
				bRun = false;
				break;

			case ' ':
				gui = !gui;
				w += b2i(gui)*3;
				break;

			case 'h':
				bHelp = !bHelp;
				break;

			case 'd':
				bDateTime = !bDateTime;
				nLines += b2i(bDateTime);
				break;

			case 'b':
				if (batSafe){
					bBatt = !bBatt;
					nLines += b2i(bBatt);
				}
				break;

			case 'c':
				bLoad = !bLoad;
				nLines += b2i(bLoad);
				break;

			case 'm':
				bMem = !bMem;
				nLines += b2i(bMem);
				break;

			case 't':
				if (tempSafe){
					bTemp = !bTemp;
					nLines += b2i(bTemp);
				}
				break;
		}
	}
	endwin();
	return 0;
}
