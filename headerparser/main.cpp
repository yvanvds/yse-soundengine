#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>
#include <boost\filesystem.hpp>

#include "dirent.h"


using namespace std;

string baseDir = "D:\\libYSE\\src\\";
string outDir = "D:\\libYSE\\include\\";

// trim from start
static inline std::string &ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
        return ltrim(rtrim(s));
}


void copyFile(string fileName) {
	cout << "parsing " << fileName << endl;
	vector<string> lines;
	ifstream file(fileName);
	string line, trimmed;
	bool add = true;
	if (file.is_open()) {
		while (file.good()) {
			getline(file, line);
			trimmed = line;
			trim(trimmed);
			if (trimmed.compare("//+++") == 0) {
				add = true;
			} else if (trimmed.compare("//---") == 0) {
				add = false;
			} else if (add) {
				lines.push_back(line);
			}
		}
	}
	file.close();

	if (lines.size()) {
		fileName.erase(0, baseDir.length());

		// create dirs if needed
		string dirs = outDir + fileName;
		while (dirs.back() != '\\') dirs.pop_back();
		boost::filesystem::create_directories(dirs);

		cout << "--> writing " << outDir + fileName << endl;
		// write file
		ofstream outfile(outDir + fileName, ios::trunc);
		for (int i = 0; i < lines.size(); i++) {
			outfile << lines[i] << endl;
		}
		outfile.close();
	}
}

void openDir(string dirName) {
	DIR * dir;
	struct dirent * ent;

	dir = opendir (dirName.c_str());
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_type == DT_DIR) {
				if (ent->d_name[0] != '.') { // omit . and ..
					string subdir;
					subdir += dirName;	
					subdir += ent->d_name;
					subdir += "\\";
					openDir(subdir);
				}

			} else {
				if (ent->d_type == DT_REG) {
					// regular file
					if (ent->d_name[ent->d_namlen - 1] == 'p' 
            && ent->d_name[ent->d_namlen - 2] == 'p'
            && ent->d_name[ent->d_namlen - 3] == 'h') {
						copyFile(dirName + ent->d_name);
					}
				}
			}
		}
		closedir(dir);
	}
}

int main() {
	boost::filesystem::remove_all(outDir);
	openDir(baseDir);

	// copy lib file
	boost::system::error_code ec;
	boost::filesystem::copy_file("D:\\libYSE\\lib\\yse.dll", "D:\\libYSE\\build\\yse.dll", boost::filesystem::copy_option::overwrite_if_exists, ec);
	boost::filesystem::copy_file("D:\\libYSE\\lib\\yse_debug.dll", "D:\\libYSE\\build\\yse_debug.dll", boost::filesystem::copy_option::overwrite_if_exists, ec);
	ec.clear();
	return 0;
}