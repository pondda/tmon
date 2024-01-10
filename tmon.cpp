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
#include <math.h>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <filesystem>
namespace fs = std::filesystem;
using Meminfo = std::unordered_map<std::string, float>;

#define BUF 256

#define INTERVAL 2000
#define TEMP_MIN 0
#define TEMP_MAX 100

// linux commands -------------------------------------------------------------------------------------------
// date & time with formatting
#define DT "date +\"%Y-%m-%d %H:%M\""

// Battery: remaining time
#define BATR "acpi | grep -o \"[0-9][0-9]:[0-9][0-9]\""

// LOAD/CPU
#define CPU "echo \"$[100-$(vmstat 1 2|tail -1|awk '{print $15}')]\""

// TEMPERATURE
#define TEMP "sensors | grep \"Core 0\" | grep -o \"[0-9]*.[0-9]°C\" | head -1"

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
	result += getCmdOut(DT);
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
	
};

Battinfo getBattinfo(std::string batdir){
	Battinfo info;

	std::ifstream s(batdir + "/status");
	s >> info.state;

	std::ifstream c(batdir + "/capacity");
	c >> info.capacity;
	return info;
}

std::string getIcon(std::string state){
	if (state ==  "Charging") return "🔌";
	return "⚡";
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
	result += getCmdOut(BATR);
	return result;
}

// LOAD/CPU ------------------------------------------------------------------------------
// this function runs in a separate thread,
// as the command takes a second to report back
void setCpu(std::atomic<float> &cpu, std::atomic<bool> &bRun){
	while (bRun){
		cpu = s2f(getCmdOut(CPU))/100.0;
	}
}

std::string getLoad(){
	std::string load;
	std::ifstream f("/proc/loadavg");
	std::getline(f, load);
	std::istringstream ss(load);
	ss >> load;
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
std::string getTemp(bool gui){
	std::string s = getCmdOut(TEMP);
	
	std::string result;
	if (gui) result += "🌡️  ";
	result += pad(s, 8);
	// result += std::format("{:<8}", s); // C++ 20+ only
	
	s = s.substr(0, s.find("°C"));
	float temp = s2f(s);
	float p = (temp - TEMP_MIN)  / (TEMP_MAX - TEMP_MIN);
	result += "[";
	if (gui) result += progBarGui(p, 7);
	else result += progBarTty(p, 7);
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
		if (bTemp && tempSafe)	{ mvprintw(y+offset, x, getTemp(gui).c_str()); offset++; }
		if (bHelp)		mvprintw(0, 0, helpStr);

		refresh();

		timeout(INTERVAL);
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
