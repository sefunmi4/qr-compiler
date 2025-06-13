#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include "peglib.h"
#include "hardware_profile.h"
#include <sstream>
#include <vector>
#include <complex>

// Very small parser generating a trivial IR used by qpp-run.
// TODO(good-first-issue): replace with a proper frontend when the language
// design stabilises.

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: qppc <source.qpp> <output.ir> [--profile file.json]\n";
        return 1;
    }
    std::ifstream input(argv[1]);
    if (!input.is_open()) {
        std::cerr << "Failed to open " << argv[1] << "\n";
        return 1;
    }
    std::ofstream out(argv[2]);
    if (!out.is_open()) {
        std::cerr << "Failed to create " << argv[2] << "\n";
        return 1;
    }
    std::ostringstream header;

    qpp::HardwareProfile profile;
    bool have_profile = false;
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--profile" && i + 1 < argc) {
            if (!qpp::load_hardware_profile(argv[i + 1], profile)) {
                std::cerr << "Failed to load hardware profile " << argv[i + 1] << "\n";
            } else {
                have_profile = true;
            }
            ++i;
        }
    }
    
    struct Instr {
        std::string op;
        std::vector<std::string> args;
    };

    std::vector<Instr> ir;
    std::vector<Instr> gate_buffer;
    int gate_count = 0;
    int qubit_count = 0;
    bool non_clifford = false;
    std::vector<std::string> used_gates;
    bool explain_next = false;
    std::string explain_override;

    auto emit_explain = [&](const std::string& msg) {
        if (explain_next) {
            std::string text = explain_override.empty() ? msg : explain_override;
            ir.push_back({"EXPLAIN", {text}});
            explain_next = false;
            explain_override.clear();
        }
    };

    auto flush_gates = [&]() {
        for (const auto& g : gate_buffer) {
            ir.push_back(g);
            gate_count++;
            used_gates.push_back(g.op);
        }
        gate_buffer.clear();
    };

    auto optimize_push = [&](const std::string& op,
                             const std::string& q,
                             const std::string& idx) {
        gate_buffer.push_back({op,{q,idx}});
        bool changed = true;
        while (changed && gate_buffer.size() >= 2) {
            changed = false;
            auto b = gate_buffer.back();
            auto& a = gate_buffer[gate_buffer.size()-2];
            if (a.args.size() == 2 && b.args.size() == 2 &&
                a.args[0] == b.args[0] && a.args[1] == b.args[1]) {
                if (a.op == b.op) {
                    if (a.op == "H" || a.op == "X" || a.op == "Y" || a.op == "Z") {
                        gate_buffer.pop_back();
                        gate_buffer.pop_back();
                        changed = true;
                        continue;
                    } else if (a.op == "S") {
                        gate_buffer.pop_back();
                        a.op = "Z";
                        changed = true;
                        continue;
                    } else if (a.op == "T") {
                        gate_buffer.pop_back();
                        a.op = "S";
                        changed = true;
                        continue;
                    }
                }
                if (a.op == "Z" && b.op == "Z") {
                    gate_buffer.pop_back();
                    gate_buffer.pop_back();
                    changed = true;
                    continue;
                }
            }
            if (gate_buffer.size() >= 3) {
                auto c = gate_buffer[gate_buffer.size()-3];
                if (c.op == "H" && b.op == "X" && a.op == "H" &&
                    c.args == a.args && c.args == b.args) {
                    gate_buffer.pop_back();
                    gate_buffer.pop_back();
                    gate_buffer.pop_back();
                    gate_buffer.push_back({"Z", {c.args[0], c.args[1]}});
                    changed = true;
                    continue;
                }
                if (c.op == "H" && b.op == "Z" && a.op == "H" &&
                    c.args == a.args && c.args == b.args) {
                    gate_buffer.pop_back();
                    gate_buffer.pop_back();
                    gate_buffer.pop_back();
                    gate_buffer.push_back({"X", {c.args[0], c.args[1]}});
                    changed = true;
                    continue;
                }
            }
        }
    };
    
    // Use a small PEG parser for basic structure validation
    peg::parser task_parser(R"(
        TASK <- 'task' '<' TARGET '>' _ NAME _ '(' PARAMS? ')' _ HINT? _ '{'
        TARGET <- 'CPU' / 'QPU' / 'AUTO'
        NAME <- [a-zA-Z_][a-zA-Z0-9_]*
        HINT <- '@' NAME
        PARAMS <- (!')' .)*
        _ <- [ \t]*
    )");
    peg::parser ctrl_parser(R"(
        IFV <- 'if' _ '(' _ NAME _ ')' _ '{'
        IFC <- 'if' _ '(' _ NAME '[' NUMBER ']' _ ')' _ '{'
        ELSE <- '}' _ 'else' _ '{'
        NAME <- [a-zA-Z_][a-zA-Z0-9_]*
        NUMBER <- [0-9]+
        _ <- [ \t]*
    )");

    std::regex task_regex(R"(task<\s*(CPU|QPU|AUTO)\s*>\s*(\w+)\s*\()") ;
    std::regex hint_regex(R"(@(dense|clifford))", std::regex::icase);
    std::regex param_qreg(R"(qregister(?:\s+\w+)?\s*(\w+)\[(\d+)\])");
    std::regex param_creg(R"(cregister(?:\s+\w+)?\s*(\w+)\[(\d+)\])");
    std::regex qalloc_regex(R"(qalloc\s+\w+\s+(\w+)\[(\d+)\];)");
    std::regex creg_regex(R"(cregister\s+\w+\s+(\w+)\[(\d+)\];)");
    std::regex gate_regex(R"((H|X|Y|Z|S|T)\((\w+)\[(\d+)\]\);)");
    std::regex cz_regex(R"(CZ\((\w+)\[(\d+)\],\s*(\w+)\[(\d+)\]\);)");
    std::regex ccx_regex(R"(CCX\((\w+)\[(\d+)\],\s*(\w+)\[(\d+)\],\s*(\w+)\[(\d+)\]\);)");
    std::regex swap_regex(R"(SWAP\((\w+)\[(\d+)\],\s*(\w+)\[(\d+)\]\);)");
    std::regex cnot_regex(R"(CX\((\w+)\[(\d+)\],\s*(\w+)\[(\d+)\]\);)");
    std::regex call_regex(R"((\w+)\s*\(\s*\);)");
    std::regex print_regex(R"(printf\(\"([^\"]*)\"\);)");
    std::regex meas_assign_regex(R"((\w+)\[(\d+)\]\s*=\s*measure\((\w+)\[(\d+)\]\);)");
    std::regex meas_var_regex(R"(int\s+(\w+)\s*=\s*measure\((\w+)\[(\d+)\]\);)");
    std::regex measure_regex(R"(measure\((\w+)\[(\d+)\]\);)");
    std::regex xor_assign_regex(R"((\w+)\[(\d+)\]\s*\^=\s*(\w+)\[(\d+)\];)");
    std::regex simple_call_regex(R"(\w+\s*\(\s*\)\s*;)");
    std::regex any_call_regex(R"(\w+\s*\([^)]*\)\s*;)");
    std::regex if_var_regex(R"(if\s*\(\s*(\w+)\s*\)\s*\{)");
    std::regex if_creg_regex(R"(if\s*\(\s*(\w+)\[(\d+)\]\s*\)\s*\{)");
    std::regex call_any_args(R"(\w+\(.*\);)");
    std::regex if_var_gate_single(R"(if\s*\(\s*(\w+)\s*\)\s*\{\s*(H|X|Y|Z|S|T)\((\w+)\[(\d+)\]\);\s*\})");
    std::regex if_creg_gate_single(R"(if\s*\(\s*(\w+)\[(\d+)\]\s*\)\s*\{\s*(H|X|Y|Z|S|T)\((\w+)\[(\d+)\]\);\s*\})");
    std::regex else_regex(R"(\}\s*else\s*\{)");


    std::string line;
    int line_no = 0;
    bool in_task = false;
    bool cond_active = false;
    bool cond_else = false;
    bool cond_pending = false; // remember last cond after closing for else
    std::string last_cond_type;
    std::string last_cond_name;
    std::string last_cond_index;
    std::string cond_type; // "var" or "creg"
    std::string cond_name;
    std::string cond_index;

    std::string current_task_name;

    while (std::getline(input, line)) {
        ++line_no;
        std::smatch m;
        // strip comments
        auto pos = line.find("//");
        if (pos != std::string::npos) line = line.substr(0, pos);
        auto trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

        if (trimmed.rfind("#explain", 0) == 0) {
            explain_next = true;
            explain_override = trimmed.substr(8);
            if (!explain_override.empty() && explain_override[0]==' ')
                explain_override.erase(0,1);
            continue;
        }

        if (trimmed.rfind("task<", 0) == 0 && !task_parser.parse(trimmed)) {
            std::cerr << "Syntax error on line " << line_no << ": " << trimmed << "\n";
            continue;
        }

        if ((trimmed.rfind("if", 0) == 0 || trimmed.find("else") != std::string::npos)
            && !ctrl_parser.parse(trimmed)
            && !(std::regex_search(trimmed, if_var_gate_single) ||
                 std::regex_search(trimmed, if_creg_gate_single))) {
            std::cerr << "Syntax error on line " << line_no << ": " << trimmed << "\n";
            continue;
        }

        if (std::regex_search(trimmed, m, task_regex)) {
            flush_gates();
            ir.push_back({"TASK", {m[2], m[1]}});
            std::size_t start = line.find('(', m.position(0));
            std::size_t end = line.find(')', start);
            std::string hint;
            std::smatch hm;
            if (end != std::string::npos && std::regex_search(line.cbegin()+end, line.cend(), hm, hint_regex))
                hint = hm[1];
            std::string hint_up;
            if (!hint.empty()) { hint_up = hint; for (auto& c : hint_up) c = toupper(c); }
            out << "TASK " << m[2] << " " << m[1];
            if (!hint_up.empty()) out << " " << hint_up;
            out << "\n";
            if (start != std::string::npos && end != std::string::npos) {
                std::string params = line.substr(start + 1, end - start - 1);
                auto begin = std::sregex_iterator(params.begin(), params.end(), param_qreg);
                auto endit = std::sregex_iterator();
                for (auto it = begin; it != endit; ++it) {
                    ir.push_back({"QALLOC", {(*it)[1], (*it)[2]}});
                }
                begin = std::sregex_iterator(params.begin(), params.end(), param_creg);
                for (auto it = begin; it != endit; ++it) {
                    ir.push_back({"CALLOC", {(*it)[1], (*it)[2]}});
                }
            }
            in_task = true;
            continue;
        }
        if (in_task && trimmed == "}" && !cond_active) {
            flush_gates();
            ir.push_back({"ENDTASK", {}});
            in_task = false;
            continue;
        }
        if (!in_task) continue;

        if (cond_active) {
            if (std::regex_search(line, m, gate_regex)) {
                flush_gates();
                emit_explain("Conditional " + std::string(m[1]) + " based on " + cond_name + (cond_type=="creg"?"["+cond_index+"]":""));
                if (cond_type == "var") {
                    ir.push_back({cond_else ? "IFNVAR" : "IFVAR",
                                   {cond_name, m[1], m[2], m[3]}});
                } else if (cond_type == "creg") {
                    ir.push_back({cond_else ? "IFNC" : "IFC",
                                   {cond_name, cond_index, m[1], m[2], m[3]}});
                }
                gate_count++;
                used_gates.push_back(m[1]);
                if (std::string(m[1]) == "T") non_clifford = true;
                continue;
            }
            if (std::regex_search(line, else_regex)) {
                cond_else = true;
                continue;
            }
            if (trimmed == "}") {
                cond_pending = true;
                last_cond_type = cond_type;
                last_cond_name = cond_name;
                last_cond_index = cond_index;
                cond_active = false;
                cond_else = false;
                continue;
            }
            continue;
        }

        if (cond_pending && trimmed == "else {") {
            cond_active = true;
            cond_else = true;
            cond_type = last_cond_type;
            cond_name = last_cond_name;
            cond_index = last_cond_index;
            cond_pending = false;
            continue;
        }

        if (std::regex_search(line, m, if_var_gate_single)) {
            flush_gates();
            emit_explain("Conditional " + std::string(m[2]) + " on " + std::string(m[1]));
            ir.push_back({"IFVAR", {m[1], m[2], m[3], m[4]}});
            gate_count++;
            used_gates.push_back(m[2]);
            continue;
        }
        if (std::regex_search(line, m, if_creg_gate_single)) {
            flush_gates();
            emit_explain("Conditional " + std::string(m[3]) + " on " + std::string(m[1]) + "[" + std::string(m[2]) + "]");
            ir.push_back({"IFC", {m[1], m[2], m[3], m[4], m[5]}});
            gate_count++;
            used_gates.push_back(m[3]);
            continue;
        }

        if (std::regex_search(line, m, if_var_regex)) {
            emit_explain("Branch on variable " + std::string(m[1]));
            cond_active = true;
            cond_else = false;
            cond_type = "var";
            cond_name = m[1];
            continue;
        }
        if (std::regex_search(line, m, if_creg_regex)) {
            emit_explain("Branch on classical bit " + std::string(m[1]) + "[" + std::string(m[2]) + "]");
            cond_active = true;
            cond_else = false;
            cond_type = "creg";
            cond_name = m[1];
            cond_index = m[2];
            continue;
        }
        if (std::regex_search(line, m, qalloc_regex)) {
            flush_gates();
            emit_explain("Allocate " + std::string(m[2]) + " qubits in " + std::string(m[1]));
            ir.push_back({"QALLOC", {m[1], m[2]}});
            qubit_count += std::stoi(m[2]);
        } else if (std::regex_search(line, m, creg_regex)) {
            flush_gates();
            emit_explain("Create classical register " + std::string(m[1]) + " of size " + std::string(m[2]));
            ir.push_back({"CALLOC", {m[1], m[2]}});
        } else if (std::regex_search(line, m, meas_var_regex)) {
            flush_gates();
            emit_explain("Measure " + std::string(m[2]) + "[" + std::string(m[3]) + "] and store in variable " + std::string(m[1]));
            ir.push_back({"VAR", {m[1]}});
            ir.push_back({"MEASURE", {m[2], m[3], "->", "VAR", m[1]}});
        } else if (std::regex_search(line, m, gate_regex)) {
            emit_explain("Apply " + std::string(m[1]) + " gate on " + std::string(m[2]) + "[" + std::string(m[3]) + "]");
            optimize_push(m[1], m[2], m[3]);
        } else if (std::regex_search(line, m, swap_regex)) {
            flush_gates();
            emit_explain("Swap " + std::string(m[1]) + "[" + std::string(m[2]) + "] with " + std::string(m[3]) + "[" + std::string(m[4]) + "]");
            if (!ir.empty() && ir.back().op == "SWAP" &&
                ir.back().args.size() == 4 &&
                ir.back().args[0] == m[1] && ir.back().args[1] == m[2] &&
                ir.back().args[2] == m[3] && ir.back().args[3] == m[4]) {
                ir.pop_back();
                gate_count--;
                used_gates.pop_back();
            } else {
                ir.push_back({"SWAP", {m[1], m[2], m[3], m[4]}});
                gate_count++;
                used_gates.push_back("SWAP");
            }
        } else if (std::regex_search(line, m, cnot_regex)) {
            flush_gates();
            emit_explain("Controlled NOT from " + std::string(m[1]) + "[" + std::string(m[2]) + "] to " + std::string(m[3]) + "[" + std::string(m[4]) + "]");
            if (!ir.empty() && ir.back().op == "CNOT" &&
                ir.back().args.size() == 4 &&
                ir.back().args[0] == m[1] && ir.back().args[1] == m[2] &&
                ir.back().args[2] == m[3] && ir.back().args[3] == m[4]) {
                ir.pop_back();
                gate_count--;
                used_gates.pop_back();
            } else {
                ir.push_back({"CNOT", {m[1], m[2], m[3], m[4]}});
                gate_count++;
                used_gates.push_back("CX");
            }
        } else if (std::regex_search(line, m, cz_regex)) {
            flush_gates();
            emit_explain("Controlled Z between " + std::string(m[1]) + "[" + std::string(m[2]) + "] and " + std::string(m[3]) + "[" + std::string(m[4]) + "]");
            if (!ir.empty() && ir.back().op == "CZ" &&
                ir.back().args.size() == 4 &&
                ir.back().args[0] == m[1] && ir.back().args[1] == m[2] &&
                ir.back().args[2] == m[3] && ir.back().args[3] == m[4]) {
                ir.pop_back();
                gate_count--;
                used_gates.pop_back();
            } else {
                ir.push_back({"CZ", {m[1], m[2], m[3], m[4]}});
                gate_count++;
                used_gates.push_back("CZ");
            }
        } else if (std::regex_search(line, m, ccx_regex)) {
            flush_gates();
            emit_explain("Toffoli on " + std::string(m[1]) + "[" + std::string(m[2]) + "], " + std::string(m[3]) + "[" + std::string(m[4]) + "] -> " + std::string(m[5]) + "[" + std::string(m[6]) + "]");
            ir.push_back({"CCX", {m[1], m[2], m[3], m[4], m[5], m[6]}});
            gate_count++;
            used_gates.push_back("CCX");
        } else if (std::regex_search(line, m, xor_assign_regex)) {
            flush_gates();
            emit_explain("Bitwise XOR expands to CNOT from " + std::string(m[3]) + "[" + std::string(m[4]) + "] to " + std::string(m[1]) + "[" + std::string(m[2]) + "]");
            ir.push_back({"CNOT", {m[3], m[4], m[1], m[2]}});
            gate_count++;
            used_gates.push_back("CX");
        } else if (std::regex_search(line, m, meas_assign_regex)) {
            flush_gates();
            emit_explain("Measure " + std::string(m[3]) + "[" + std::string(m[4]) + "] into " + std::string(m[1]) + "[" + std::string(m[2]) + "]");
            ir.push_back({"MEASURE", {m[3], m[4], "->", m[1], m[2]}});
            gate_count++;
            used_gates.push_back("MEASURE");
        } else if (std::regex_search(line, m, measure_regex)) {
            flush_gates();
            emit_explain("Measure " + std::string(m[1]) + "[" + std::string(m[2]) + "]");
            ir.push_back({"MEASURE", {m[1], m[2]}});
            gate_count++;
            used_gates.push_back("MEASURE");
        } else if (std::regex_search(trimmed, call_regex)) {
            // ignore simple function calls
        } else if (trimmed.size() > 0) {
            std::cerr << "Unrecognized syntax on line " << line_no << ": " << trimmed << "\n";
        }
    }

    flush_gates();
    if (have_profile &&
        !qpp::check_profile_limits(profile, qubit_count, gate_count,
                                   used_gates, std::cerr)) {
        return 1;
    }
    std::size_t bytes = std::size_t(1) << qubit_count;
    bytes *= sizeof(std::complex<double>);

    header << "ENGINE " << (non_clifford ? "DENSE" : "STABILIZER") << "\n";
    header << "#QUBITS " << qubit_count << "\n";
    header << "#GATES " << gate_count << "\n";
    header << "#BYTES " << bytes << "\n";
    header << "CLIFFORD " << (non_clifford ? 0 : 1) << "\n";
    out << header.str();

    for (const auto& ins : ir) {
        out << ins.op;
        for (const auto& a : ins.args) out << " " << a;
        out << "\n";
    }

    std::cout << "Compilation complete. Estimated memory: "
              << bytes << " bytes" << std::endl;
    return 0;
}
