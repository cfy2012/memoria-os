#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <vector>
#include <algorithm>
#include <fstream>

using namespace std;

// 单参数原生C函数类型定义
typedef double (*NativeFunc1)(double);

string toUpperStr(const string& s) {
	string res = s;
	for (int i = 0; i < (int)res.length(); ++i) {
		res[i] = static_cast<char>(toupper(static_cast<unsigned char>(res[i])));
	}
	return res;
}

string toLowerStr(const string& s) {
	string res = s;
	for (int i = 0; i < (int)res.length(); ++i) {
		res[i] = static_cast<char>(tolower(static_cast<unsigned char>(res[i])));
	}
	return res;
}

string trimStr(const string& s) {
	if (s.empty()) return s;
	int start = 0;
	while (start < (int)s.length() && s[start] == ' ') start++;
	int end = (int)s.length() - 1;
	while (end >= 0 && s[end] == ' ') end--;
	if (start > end) return "";
	return s.substr(start, end - start + 1);
}

bool isNumericStr(const string& s) {
	string t = trimStr(s);
	if (t.empty()) return false;
	int start = 0;
	if (t[0] == '-' || t[0] == '+') start = 1;
	if (start >= (int)t.length()) return false;

	bool hasDot = false;
	for (int i = start; i < (int)t.length(); i++) {
		if (t[i] == '.') {
			if (hasDot) return false;
			hasDot = true;
		} else if (!isdigit(static_cast<unsigned char>(t[i]))) {
			return false;
		}
	}
	return true;
}

bool checkBracketBalanced(const string &expr) {
	int cnt = 0;
	for (size_t i = 0; i < expr.size(); i++) {
		if (expr[i] == '(') cnt++;
		if (expr[i] == ')') cnt--;
		if (cnt < 0) return false;
	}
	return cnt == 0;
}

// 加载 .bas 文件
bool loadBasicFile(class BasicInterpreter &interp, const string &filename) {
	ifstream fin(filename.c_str());
	if (!fin.is_open()) {
		cout << "Error: Cannot open file: " << filename << endl;
		return false;
	}
	string line;
	int lineCount = 0;
	while (getline(fin, line)) {
		string trimmed = trimStr(line);
		if (trimmed.empty()) continue;

		istringstream iss(trimmed);
		int ln;
		if (iss >> ln) {
			string rest;
			getline(iss, rest);
			interp.addLine(ln, rest);
			lineCount++;
		}
	}
	fin.close();
	cout << "Loaded " << lineCount << " lines from " << filename << endl;
	return true;
}

// --- BASIC 解释器类（带C库桥接层）---
class BasicInterpreter {
	private:
		map<int, string> programLines;
		map<string, double> variables;
		vector<int> gosubStack;
		int currentLineNum;
		bool isRunning;

		// C库桥接表：单参数函数
		map<string, NativeFunc1> nativeTable;

		double evaluateExpression(const string& expr);
		bool evaluateCondition(const string& cond);
		void goToNextLine();

		void executeLine(const string& line);
		void doPrint(const string& args);
		void doLet(const string& args);
		void doInput(const string& args);
		void doGoto(const string& args);
		void doIf(const string& fullLine);
		void doSystem(const string& args);
		void doGosub(const string& args);
		void doReturn();

		// 初始化桥接表
		void initNativeBridge() {
			nativeTable["SIN"]   = sin;
			nativeTable["COS"]   = cos;
			nativeTable["TAN"]   = tan;
			nativeTable["ABS"]   = fabs;
			nativeTable["SQR"]   = sqrt;
			nativeTable["INT"]   = floor;
			nativeTable["LOG"]   = log;
			nativeTable["LOG10"] = log10;
			nativeTable["EXP"]   = exp;
		}

	public:
		BasicInterpreter() : currentLineNum(0), isRunning(false) {
			initNativeBridge();
		}

		void addLine(int lineNum, const string& code) {
			if (trimStr(code).empty()) {
				programLines.erase(lineNum);
			} else {
				programLines[lineNum] = code;
			}
		}

		void listProgram() {
			if (programLines.empty()) {
				cout << "Empty program." << endl;
				return;
			}
			for (auto &p : programLines) {
				cout << p.first << " " << p.second << endl;
			}
		}

		void clearVars() {
			variables.clear();
			gosubStack.clear();
		}

		void run();
};

double BasicInterpreter::evaluateExpression(const string& expr) {
	string e = trimStr(expr);
	if (e.empty()) return 0;
	if (!checkBracketBalanced(e)) {
		cout << "Error: Unbalanced parentheses" << endl;
		return 0;
	}

	// 一元负号
	if (e[0] == '-') {
		string sub = e.substr(1);
		return -evaluateExpression(sub);
	}

	// 数字常量
	if (isNumericStr(e)) {
		return atof(e.c_str());
	}

	// 函数调用：FUNC(...)
	size_t openParen = e.find('(');
	size_t closeParen = e.rfind(')');
	if (openParen != string::npos && closeParen != string::npos && closeParen > openParen) {
		string funcName = toUpperStr(e.substr(0, openParen));
		string argText = trimStr(e.substr(openParen + 1, closeParen - openParen - 1));

		// 桥接查表调用C原生单参数函数
		if (nativeTable.find(funcName) != nativeTable.end()) {
			double argVal = evaluateExpression(argText);
			NativeFunc1 fn = nativeTable[funcName];

			if (funcName == "SQR" && argVal < 0) {
				cout << "Error: SQR negative argument" << endl;
				return 0;
			}
			if ((funcName == "LOG" || funcName == "LOG10") && argVal <= 0) {
				cout << "Error: Log argument <= 0" << endl;
				return 0;
			}
			return fn(argVal);
		}

		// POW(a,b) 双参数特殊函数
		if (funcName == "POW") {
			size_t commaPos = argText.find(',');
			if (commaPos == string::npos) {
				cout << "Syntax Error: POW needs two arguments POW(a,b)" << endl;
				return 0;
			}
			string aStr = trimStr(argText.substr(0, commaPos));
			string bStr = trimStr(argText.substr(commaPos + 1));
			double a = evaluateExpression(aStr);
			double b = evaluateExpression(bStr);
			return pow(a, b);
		}
	}

	// 变量查找
	string lowerE = toLowerStr(e);
	if (variables.count(lowerE)) {
		return variables[lowerE];
	}

	// 加减
	for (int i = (int)e.length() - 1; i >= 0; i--) {
		if (e[i] == '+' || e[i] == '-') {
			if (i == 0) continue;
			if (e[i - 1] == '(') continue;
			double left = evaluateExpression(e.substr(0, i));
			double right = evaluateExpression(e.substr(i + 1));
			if (e[i] == '+') return left + right;
			if (e[i] == '-') return left - right;
		}
	}
	// 乘除
	for (int i = (int)e.length() - 1; i >= 0; i--) {
		if (e[i] == '*' || e[i] == '/') {
			double left = evaluateExpression(e.substr(0, i));
			double right = evaluateExpression(e.substr(i + 1));
			if (e[i] == '*') return left * right;
			if (e[i] == '/') {
				if (fabs(right) < 1e-12) {
					cout << "Error: Division by zero" << endl;
					return 0;
				}
				return left / right;
			}
		}
	}

	// 剥外层括号
	if (e[0] == '(' && e[e.size() - 1] == ')') {
		return evaluateExpression(e.substr(1, e.size() - 2));
	}

	cout << "Error: Unknown expression: " << e << endl;
	return 0;
}

bool BasicInterpreter::evaluateCondition(const string& cond) {
	string c = trimStr(cond);
	size_t pos;
	string op;
	// 支持比较运算符
	if ((pos = c.find(">=")) != string::npos) op = ">=";
	else if ((pos = c.find("<=")) != string::npos) op = "<=";
	else if ((pos = c.find("<>")) != string::npos) op = "<>";
	else if ((pos = c.find(">")) != string::npos) op = ">";
	else if ((pos = c.find("<")) != string::npos) op = "<";
	else if ((pos = c.find("=")) != string::npos) op = "=";
	else {
		cout << "Error: Invalid condition" << endl;
		return false;
	}
	string leftStr = trimStr(c.substr(0, pos));
	string rightStr = trimStr(c.substr(pos + op.size()));
	double L = evaluateExpression(leftStr);
	double R = evaluateExpression(rightStr);

	if (op == "=")  return L == R;
	if (op == ">")  return L > R;
	if (op == "<")  return L < R;
	if (op == ">=") return L >= R;
	if (op == "<=") return L <= R;
	if (op == "<>") return L != R;
	return false;
}

void BasicInterpreter::doPrint(const string& args) {
	string a = trimStr(args);
	// 简单字符串检测，双引号
	if (a.size() >= 2 && a[0] == '"' && a.back() == '"') {
		cout << a.substr(1, a.size() - 2) << endl;
		return;
	}
	double v = evaluateExpression(a);
	cout << v << endl;
}

void BasicInterpreter::doLet(const string& args) {
	size_t eq = args.find('=');
	if (eq == string::npos) {
		cout << "Syntax Error LET" << endl;
		return;
	}
	string varName = toLowerStr(trimStr(args.substr(0, eq)));
	string exprStr = trimStr(args.substr(eq + 1));
	double val = evaluateExpression(exprStr);
	variables[varName] = val;
}

void BasicInterpreter::doInput(const string& args) {
	string varName = toLowerStr(trimStr(args));
	double v;
	cout << "? ";
	cin >> v;
	variables[varName] = v;
}

void BasicInterpreter::doGoto(const string& args) {
	double v = evaluateExpression(args);
	currentLineNum = (int)v;
}

void BasicInterpreter::doGosub(const string& args) {
	gosubStack.push_back(currentLineNum);
	double v = evaluateExpression(args);
	currentLineNum = (int)v;
}

void BasicInterpreter::doReturn() {
	if (gosubStack.empty()) {
		cout << "Error: RETURN without GOSUB" << endl;
		isRunning = false;
		return;
	}
	currentLineNum = gosubStack.back();
	gosubStack.pop_back();
}

void BasicInterpreter::doIf(const string& fullLine) {
	size_t thenPos = toUpperStr(fullLine).find("THEN");
	if (thenPos == string::npos) {
		cout << "Syntax Error IF missing THEN" << endl;
		return;
	}
	string condStr = trimStr(fullLine.substr(0, thenPos));
	string thenBody = trimStr(fullLine.substr(thenPos + 4));
	if (evaluateCondition(condStr)) {
		// THEN后面如果是数字就是GOTO，否则直接执行语句
		if (isNumericStr(thenBody)) {
			doGoto(thenBody);
		} else {
			executeLine(thenBody);
		}
	}
}

void BasicInterpreter::doSystem(const string& args) {
	string cmd = trimStr(args);
	if (cmd.size() >= 2 && cmd[0] == '"' && cmd.back() == '"') {
		cmd = cmd.substr(1, cmd.size() - 2);
	}
	system(cmd.c_str());
}

void BasicInterpreter::executeLine(const string& line) {
	string uline = toUpperStr(line);
	if (uline.substr(0, 5) == "PRINT") {
		doPrint(line.substr(5));
	} else if (uline.substr(0, 3) == "LET") {
		doLet(line.substr(3));
	} else if (uline.substr(0, 5) == "INPUT") {
		doInput(line.substr(5));
	} else if (uline.substr(0, 4) == "GOTO") {
		doGoto(line.substr(4));
	} else if (uline.substr(5, 5) == "GOSUB") {
		doGosub(line.substr(5));
	} else if (uline.substr(0, 6) == "RETURN") {
		doReturn();
	} else if (uline.substr(0, 2) == "IF") {
		doIf(line);
	} else if (uline.substr(0, 6) == "SYSTEM") {
		doSystem(line.substr(6));
	} else if (uline.substr(0, 4) == "END") {
		isRunning = false;
	} else {
		cout << "Unknown command: " << line << endl;
	}
}

void BasicInterpreter::run() {
	if (programLines.empty()) {
		cout << "No program to run." << endl;
		return;
	}
	isRunning = true;
	currentLineNum = programLines.begin()->first;
	gosubStack.clear();

	int safetyCounter = 0;
	const int MAX_STEP = 100000;
	while (isRunning && safetyCounter < MAX_STEP) {
		safetyCounter++;
		auto it = programLines.find(currentLineNum);
		if (it == programLines.end()) {
			it = programLines.lower_bound(currentLineNum);
			if (it == programLines.end()) {
				isRunning = false;
				break;
			}
			currentLineNum = it->first;
			continue;
		}
		string lineContent = it->second;
		executeLine(lineContent);
		if (isRunning) goToNextLine();
	}

	if (safetyCounter >= MAX_STEP) {
		cout << "Error: Possible infinite loop detected." << endl;
	}
	isRunning = false;
}

void BasicInterpreter::goToNextLine() {
	auto it = programLines.upper_bound(currentLineNum);
	if (it == programLines.end()) {
		isRunning = false;
	} else {
		currentLineNum = it->first;
	}
}

int main() {
	BasicInterpreter interp;
	string input;
	cout << "MiniBasic with Native Bridge + INCLUDE. Type HELP for commands." << endl;
	while (true) {
		cout << "OK > ";
		getline(cin, input);
		string cmd = trimStr(input);
		if (cmd.empty()) continue;
		string ucmd = toUpperStr(cmd);

		if (ucmd == "EXIT" || ucmd == "QUIT") break;
		if (ucmd == "LIST") {
			interp.listProgram();
			continue;
		}
		if (ucmd == "RUN") {
			interp.clearVars();
			interp.run();
			continue;
		}
		if (ucmd == "CLEAR") {
			interp = BasicInterpreter();
			cout << "Program cleared." << endl;
			continue;
		}
		if (ucmd == "HELP") {
			cout << "Commands:\n";
			cout << "  LIST        show program\n";
			cout << "  RUN         execute\n";
			cout << "  CLEAR       erase program\n";
			cout << "  INCLUDE \"file.bas\" load library\n";
			cout << "  EXIT        quit\n";
			cout << "BASIC builtin: SIN,COS,TAN,ABS,SQR,INT,LOG,LOG10,EXP,POW\n";
			continue;
		}
		// INCLUDE REPL指令
		if (ucmd.substr(0, 7) == "INCLUDE") {
			string arg = trimStr(cmd.substr(7));
			if (arg.size() >= 2 && arg[0] == '"' && arg.back() == '"') {
				arg = arg.substr(1, arg.size() - 2);
			}
			loadBasicFile(interp, arg);
			continue;
		}

		// 行号开头，添加代码到程序
		istringstream iss(cmd);
		int ln;
		if (iss >> ln) {
			string rest;
			getline(iss, rest);
			interp.addLine(ln, rest);
			continue;
		}
		cout << "Unknown command." << endl;
	}
	cout << "Bye." << endl;
	return 0;
}

