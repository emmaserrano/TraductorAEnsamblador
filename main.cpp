#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <regex>
#include <map>

struct ParTraduccion {
    std::string c_code;
    std::string asm_code;
};

class MiniCompilador {
private:
    std::map<std::string, std::string> variables;
    int contadorEtiquetas = 1;
    int contadorFormatos = 1;
    std::vector<std::string> pilaCierres;

    // Asigna un registro a cada variable que el usuario invente
    std::string obtenerRegistro(const std::string& var) {
        // Si es un número constante, lo devuelve tal cual
        if (std::regex_match(var, std::regex("^-?[0-9]+$"))) {
            return var;
        }
        if (variables.find(var) == variables.end()) {
            // Orden didáctico de registros
            std::vector<std::string> regs = {"eax", "ecx", "edi", "esi", "edx", "ebx"};
            int idx = variables.size() % regs.size();
            variables[var] = regs[idx];
        }
        return variables[var];
    }

    std::string limpiar(std::string s) {
        s.erase(0, s.find_first_not_of(" \t"));
        size_t last = s.find_last_not_of(" \t");
        if (last != std::string::npos) s.erase(last + 1);
        return s;
    }

    // Invierte el operador para saltar a la salida cuando la condición falla
    std::string saltoInverso(const std::string& op) {
        if (op == "<")  return "jge";
        if (op == ">")  return "jle";
        if (op == "<=") return "jg";
        if (op == ">=") return "jl";
        if (op == "==") return "jne";
        if (op == "!=") return "je";
        return "jmp";
    }

    std::string escaparCadenaAsm(const std::string& cadena) {
        std::string resultado = "\"";
        for (size_t i = 0; i < cadena.size(); ++i) {
            if (cadena[i] == '\\' && i + 1 < cadena.size()) {
                char siguiente = cadena[++i];
                if (siguiente == 'n') resultado += "\", 10, \"";
                else if (siguiente == 't') resultado += "\", 9, \"";
                else if (siguiente == '\\') resultado += "\\\\";
                else resultado += siguiente;
            } else if (cadena[i] == '"') {
                resultado += "\\\"";
            } else {
                resultado += cadena[i];
            }
        }
        resultado += "\"";
        if (resultado.size() >= 4 &&
            resultado.compare(resultado.size() - 4, 4, ", \"\"") == 0) {
            resultado.erase(resultado.size() - 4);
        }
        return resultado;
    }

    std::string obtenerFinBucle() const {
        for (auto i = pilaCierres.rbegin(); i != pilaCierres.rend(); ++i) {
            if (i->rfind("FOR|", 0) == 0) {
                size_t p1 = i->find('|', 4);
                size_t p2 = i->find('|', p1 + 1);
                size_t p3 = i->find('|', p2 + 1);
                return i->substr(p2 + 1, p3 - p2 - 1);
            }
            if (i->rfind("WHILE|", 0) == 0) {
                size_t p = i->find('|', 6);
                return i->substr(p + 1);
            }
            if (i->rfind("SWITCH|", 0) == 0) {
                size_t p1 = i->find('|', 7);
                size_t p2 = i->find('|', p1 + 1);
                return i->substr(p1 + 1, p2 - p1 - 1);
            }
        }
        return "";
    }

    std::string obtenerInicioBucle() const {
        for (auto i = pilaCierres.rbegin(); i != pilaCierres.rend(); ++i) {
            if (i->rfind("FOR|", 0) == 0) {
                size_t p1 = i->find('|', 4);
                size_t p2 = i->find('|', p1 + 1);
                size_t p3 = i->find('|', p2 + 1);
                return i->substr(p3 + 1);
            }
            if (i->rfind("WHILE|", 0) == 0) {
                return i->substr(6, i->find('|', 6) - 6);
            }
        }
        return "";
    }

public:
    std::vector<ParTraduccion> traducirBloque(const std::vector<std::string>& lineas) {
        std::vector<ParTraduccion> salida;
        variables.clear();
        pilaCierres.clear();
        contadorEtiquetas = 1;
        contadorFormatos = 1;

        std::smatch m;

        for (const auto& lineaOriginal : lineas) {
            std::string l = limpiar(lineaOriginal);
            if (l.empty()) continue;

            // 1. Directivas del preprocesador, como #include <stdio.h>
            if (l.rfind("#include", 0) == 0) {
                salida.push_back({lineaOriginal, "    ; Directiva de compilacion: " + l});
            }
            // 2. Declaración de función: int nombre(int a, int b) {
            else if (std::regex_search(l, m, std::regex(R"(int\s+(\w+)\s*\((.*)\)\s*\{?)"))) {
                std::string nombreFunc = m[1];
                std::string params = m[2];

                // Si hay parámetros, los asignamos primero a edi y esi (convención x86-64)
                std::regex paramRegex(R"(int\s+(\w+))");
                auto inicio = std::sregex_iterator(params.begin(), params.end(), paramRegex);
                auto fin = std::sregex_iterator();
                std::vector<std::string> regParams = {"edi", "esi", "edx", "ecx"};
                int pIdx = 0;
                for (auto i = inicio; i != fin && pIdx < 4; ++i, ++pIdx) {
                    variables[(*i)[1]] = regParams[pIdx];
                }

                salida.push_back({lineaOriginal, nombreFunc + ":"});
                pilaCierres.push_back("FUNC");
            }
            // 3. printf: printf("Texto %d\n", variable);
            else if (std::regex_search(l, m, std::regex(R"REGEX(printf\s*\(\s*"((?:\\.|[^"\\])*)"(?:\s*,\s*(.*?))?\s*\)\s*;)REGEX"))) {
                std::string formato = m[1];
                std::string argumentos = m[2];
                std::string etiquetaFormato = "formato_" + std::to_string(contadorFormatos++);

                salida.push_back({lineaOriginal, "    lea rdi, [rel " + etiquetaFormato + "]"});
                if (!argumentos.empty()) {
                    std::vector<std::string> registros = {"rsi", "rdx", "rcx", "r8", "r9"};
                    size_t inicio = 0;
                    size_t indice = 0;
                    while (inicio < argumentos.size() && indice < registros.size()) {
                        size_t coma = argumentos.find(',', inicio);
                        std::string argumento = limpiar(argumentos.substr(inicio, coma - inicio));
                        salida.push_back({"", "    mov " + registros[indice++] + ", " + obtenerRegistro(argumento)});
                        if (coma == std::string::npos) break;
                        inicio = coma + 1;
                    }
                }
                salida.push_back({"", "    xor eax, eax"});
                salida.push_back({"", "    call printf"});
                salida.push_back({"", etiquetaFormato + ": db " + escaparCadenaAsm(formato) + ", 0"});
            }
            // 4. Switch: switch (opcion) {
            else if (std::regex_search(l, m, std::regex(R"(switch\s*\(\s*(\w+)\s*\)\s*\{?)"))) {
                std::string registro = obtenerRegistro(m[1]);
                std::string fin = "fin_switch_" + std::to_string(contadorEtiquetas++);
                pilaCierres.push_back("SWITCH|" + registro + "|" + fin + "||0");
                salida.push_back({lineaOriginal, "    ; Inicio de switch sobre " + registro});
            }
            // 5. Case numerico: case 1:
            else if (std::regex_search(l, m, std::regex(R"(case\s+(-?\d+)\s*:)"))) {
                for (auto i = pilaCierres.rbegin(); i != pilaCierres.rend(); ++i) {
                    if (i->rfind("SWITCH|", 0) != 0) continue;

                    size_t p1 = i->find('|', 7);
                    size_t p2 = i->find('|', p1 + 1);
                    size_t p3 = i->find('|', p2 + 1);
                    std::string registro = i->substr(7, p1 - 7);
                    std::string fin = i->substr(p1 + 1, p2 - p1 - 1);
                    std::string casoAnterior = i->substr(p2 + 1, p3 - p2 - 1);
                    bool yaTieneCaso = i->substr(p3 + 1) == "1";
                    std::string siguiente = "siguiente_case_" + std::to_string(contadorEtiquetas++);
                    std::string etiquetaCaso = "case_" + std::to_string(contadorEtiquetas++);

                    if (yaTieneCaso) salida.push_back({"", "    " + casoAnterior + ":"});
                    salida.push_back({lineaOriginal, "    cmp " + registro + ", " + m.str(1)});
                    salida.push_back({"", "    jne " + siguiente});
                    salida.push_back({"", etiquetaCaso + ":"});
                    *i = "SWITCH|" + registro + "|" + fin + "|" + siguiente + "|1";
                    break;
                }
            }
            // 6. Default: se alcanza cuando ningún case coincide
            else if (l == "default:") {
                for (auto i = pilaCierres.rbegin(); i != pilaCierres.rend(); ++i) {
                    if (i->rfind("SWITCH|", 0) != 0) continue;
                    size_t p1 = i->find('|', 7);
                    size_t p2 = i->find('|', p1 + 1);
                    size_t p3 = i->find('|', p2 + 1);
                    std::string fin = i->substr(p1 + 1, p2 - p1 - 1);
                    std::string casoAnterior = i->substr(p2 + 1, p3 - p2 - 1);
                    if (!casoAnterior.empty()) salida.push_back({"", "    " + casoAnterior + ":"});
                    std::string etiquetaDefault = "default_" + std::to_string(contadorEtiquetas++);
                    salida.push_back({lineaOriginal, "    " + etiquetaDefault + ":"});
                    *i = "SWITCH|" + i->substr(7, p1 - 7) + "|" + fin + "||1";
                    break;
                }
            }
            // 7. Ciclo FOR: for (int i = 0; i < 5; i++) {  o  for (; i < 3; i++) {
            else if (std::regex_search(l, m, std::regex(R"(for\s*\(\s*(?:int\s+)?(\w+)?\s*=?\s*(-?\d+)?\s*;\s*(\w+)\s*(<|>|<=|>=|==|!=)\s*(\w+)\s*;\s*(\w+)(\+\+|--)\s*\)\s*\{?)"))) {
                std::string varInit = m[1], valInit = m[2];
                std::string varCond = m[3], op = m[4], limite = m[5];
                std::string varInc = m[6], incOp = m[7];

                std::string regCond;
                if (!varCond.empty() && variables.find(varCond) == variables.end()) {
                    variables[varCond] = "ecx"; // Forzar ecx para contadores de bucle
                }
                regCond = obtenerRegistro(varCond);

                if (!varInit.empty() && !valInit.empty()) {
                    salida.push_back({"", "    mov " + regCond + ", " + valInit});
                }

                std::string etiqBucle = "bucle_" + std::to_string(contadorEtiquetas);
                std::string etiqFin = "fin_" + std::to_string(contadorEtiquetas++);

                salida.push_back({lineaOriginal, etiqBucle + ":"});
                salida.push_back({"", "    cmp " + regCond + ", " + obtenerRegistro(limite)});
                salida.push_back({"", "    " + saltoInverso(op) + " " + etiqFin});

                std::string instruccionPaso = (incOp == "++") ? "inc " + regCond : "dec " + regCond;
                std::string etiqPaso = "paso_" + std::to_string(contadorEtiquetas++);
                pilaCierres.push_back("FOR|" + instruccionPaso + "|" + etiqBucle + "|" + etiqFin + "|" + etiqPaso);
            }
            // 8. Ciclo WHILE: while (a > 0) {
            else if (std::regex_search(l, m, std::regex(R"(while\s*\(\s*(\w+)\s*(<|>|<=|>=|==|!=)\s*(\w+)\s*\)\s*\{?)"))) {
                std::string varCond = m[1], op = m[2], limite = m[3];
                std::string regCond = obtenerRegistro(varCond);

                std::string etiqVal = "validar_" + std::to_string(contadorEtiquetas);
                std::string etiqFin = "fin_" + std::to_string(contadorEtiquetas++);

                salida.push_back({lineaOriginal, etiqVal + ":"});
                salida.push_back({"", "    cmp " + regCond + ", " + obtenerRegistro(limite)});
                salida.push_back({"", "    " + saltoInverso(op) + " " + etiqFin});

                pilaCierres.push_back("WHILE|" + etiqVal + "|" + etiqFin);
            }
            // 9. break y continue dentro del ciclo más cercano o switch
            else if (l == "break;" || l == "break") {
                std::string fin = obtenerFinBucle();
                if (!fin.empty()) {
                    salida.push_back({lineaOriginal, "    jmp " + fin});
                } else {
                    salida.push_back({lineaOriginal, "    ; [break fuera de un ciclo]"});
                }
            }
            else if (l == "continue;" || l == "continue") {
                std::string inicio = obtenerInicioBucle();
                if (!inicio.empty()) {
                    salida.push_back({lineaOriginal, "    jmp " + inicio});
                } else {
                    salida.push_back({lineaOriginal, "    ; [continue fuera de un ciclo]"});
                }
            }
            // 10. Condicional IF simple en una línea: if (a > b) return a;
            else if (std::regex_search(l, m, std::regex(R"(if\s*\(\s*(\w+)\s*(<|>|<=|>=|==|!=)\s*(\w+)\s*\)\s*return\s+(\w+)\s*;)"))) {
                std::string izq = obtenerRegistro(m[1]);
                std::string op = m[2];
                std::string der = obtenerRegistro(m[3]);
                std::string retVal = obtenerRegistro(m[4]);

                std::map<std::string, std::string> saltoDirecto = {
                    {">", "jg"}, {"<", "jl"}, {">=", "jge"}, {"<=", "jle"}, {"==", "je"}, {"!=", "jne"}
                };

                salida.push_back({lineaOriginal, "    mov eax, " + retVal});
                salida.push_back({"", "    cmp " + izq + ", " + der});
                salida.push_back({"", "    " + saltoDirecto[op] + " fin_ret"});
            }
            // 7. Operación compuesta: a += 2;  o  a -= 5;
            else if (std::regex_search(l, m, std::regex(R"((\w+)\s*(\+=|-=)\s*(-?\d+|\w+)\s*;)"))) {
                std::string regDest = obtenerRegistro(m[1]);
                std::string op = (m[2] == "+=") ? "add" : "sub";
                std::string orig = obtenerRegistro(m[3]);
                salida.push_back({lineaOriginal, "    " + op + " " + regDest + ", " + orig});
            }
            // 8. Incremento / Decremento suelto: a++; o a--;
            else if (std::regex_search(l, m, std::regex(R"((\w+)(\+\+|--)\s*;)"))) {
                std::string reg = obtenerRegistro(m[1]);
                std::string op = (m[2] == "++") ? "inc " : "sub ";
                std::string sufijo = (m[2] == "++") ? "" : ", 1";
                salida.push_back({lineaOriginal, "    " + op + reg + sufijo});
            }
            // 9. Suma o Resta binaria: (int) c = a + b;
            else if (std::regex_search(l, m, std::regex(R"((?:int\s+)?(\w+)\s*=\s*(-?\d+|\w+)\s*(\+|-)\s*(-?\d+|\w+)\s*;)"))) {
                std::string dest = obtenerRegistro(m[1]);
                std::string op1 = obtenerRegistro(m[2]);
                std::string operador = (m[3] == "+") ? "add" : "sub";
                std::string op2 = obtenerRegistro(m[4]);

                if (dest != op1) {
                    salida.push_back({lineaOriginal, "    mov " + dest + ", " + op1});
                    salida.push_back({"", "    " + operador + " " + dest + ", " + op2});
                } else {
                    salida.push_back({lineaOriginal, "    " + operador + " " + dest + ", " + op2});
                }
            }
            // 10. Asignación simple: int a = 10;  o  a = b;
            else if (std::regex_search(l, m, std::regex(R"((?:int\s+)?(\w+)\s*=\s*(-?\d+|\w+)\s*;)"))) {
                std::string dest = obtenerRegistro(m[1]);
                std::string val = obtenerRegistro(m[2]);
                salida.push_back({lineaOriginal, "    mov " + dest + ", " + val});
            }
            // 11. Return: return a; o return 0;
            else if (std::regex_search(l, m, std::regex(R"(return\s+(-?\d+|\w+)\s*;)"))) {
                std::string val = obtenerRegistro(m[1]);
                if (val != "eax") {
                    salida.push_back({lineaOriginal, "    mov eax, " + val});
                    salida.push_back({"", "fin_ret:"});
                    salida.push_back({"", "    ret"});
                } else {
                    salida.push_back({lineaOriginal, "fin_ret:"});
                    salida.push_back({"", "    ret"});
                }
            }
            // 10. Llave de cierre: }
            else if (l == "}") {
                if (!pilaCierres.empty()) {
                    std::string tope = pilaCierres.back();
                    pilaCierres.pop_back();

                    if (tope.rfind("FOR|", 0) == 0) {
                        // Formato: FOR|inc ecx|bucle_1|fin_1|paso_1
                        size_t p1 = tope.find('|', 4);
                        size_t p2 = tope.find('|', p1 + 1);
                        size_t p3 = tope.find('|', p2 + 1);
                        std::string paso = tope.substr(4, p1 - 4);
                        std::string bucle = tope.substr(p1 + 1, p2 - p1 - 1);
                        std::string fin = tope.substr(p2 + 1, p3 - p2 - 1);
                        std::string etiqPaso = tope.substr(p3 + 1);

                        salida.push_back({"", "    " + etiqPaso + ":"});
                        salida.push_back({lineaOriginal, "    " + paso});
                        salida.push_back({"", "    jmp " + bucle});
                        salida.push_back({"", fin + ":"});
                    } 
                    else if (tope.rfind("WHILE|", 0) == 0) {
                        size_t p1 = tope.find('|', 6);
                        std::string val = tope.substr(6, p1 - 6);
                        std::string fin = tope.substr(p1 + 1);

                        salida.push_back({lineaOriginal, "    jmp " + val});
                        salida.push_back({"", fin + ":"});
                    } 
                    else if (tope.rfind("SWITCH|", 0) == 0) {
                        size_t p1 = tope.find('|', 7);
                        size_t p2 = tope.find('|', p1 + 1);
                        size_t p3 = tope.find('|', p2 + 1);
                        std::string fin = tope.substr(p1 + 1, p2 - p1 - 1);
                        std::string siguiente = tope.substr(p2 + 1, p3 - p2 - 1);

                        if (!siguiente.empty()) salida.push_back({"", "    " + siguiente + ":"});
                        salida.push_back({lineaOriginal, "    " + fin + ":"});
                    }
                    else {
                        salida.push_back({lineaOriginal, ""});
                    }
                }
            } 
            else {
                salida.push_back({lineaOriginal, "    ; [No reconocido]"});
            }
        }
        return salida;
    }
};

int main() {
    MiniCompilador compilador;
    std::string linea;

    std::cout << "=====================================================================\n";
    std::cout << "  TRADUCTOR INTERACTIVO DE C++ A ENSAMBLADOR x86-64\n";
    std::cout << "=====================================================================\n";
    std::cout << "Escribe tu codigo C++ linea por linea.\n";
    std::cout << "Escribe 'TRADUCIR' en una linea nueva para ver el resultado,\n";
    std::cout << "o 'SALIR' para terminar el programa.\n";

    while (true) {
        std::vector<std::string> codigoUsuario;
        std::cout << "\n--- Ingresa tu codigo C++ (termina con TRADUCIR) ---\n";

        while (std::getline(std::cin, linea)) {
            if (linea == "SALIR" || linea == "salir") return 0;
            if (linea == "TRADUCIR" || linea == "traducir") break;
            codigoUsuario.push_back(linea);
        }

        if (codigoUsuario.empty()) continue;

        auto resultado = compilador.traducirBloque(codigoUsuario);

        std::cout << "\n=====================================================================\n";
        std::cout << std::left << std::setw(35) << "C / C++ (Tu Input)" << " | Ensamblador x86-64 Generado\n";
        std::cout << "---------------------------------------------------------------------\n";
        for (const auto& par : resultado) {
            std::cout << std::left << std::setw(35) << par.c_code << " | " << par.asm_code << "\n";
        }
        std::cout << "=====================================================================\n";
    }

    return 0;
}