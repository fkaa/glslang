//
//Copyright (C) 2002-2005  3Dlabs Inc. Ltd.
//Copyright (C) 2013 LunarG, Inc.
//All rights reserved.
//
//Redistribution and use in source and binary forms, with or without
//modification, are permitted provided that the following conditions
//are met:
//
//    Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
//THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
//ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//POSSIBILITY OF SUCH DAMAGE.
//
/****************************************************************************\
Copyright (c) 2002, NVIDIA Corporation.

NVIDIA Corporation("NVIDIA") supplies this software to you in
consideration of your agreement to the following terms, and your use,
installation, modification or redistribution of this NVIDIA software
constitutes acceptance of these terms.  If you do not agree with these
terms, please do not use, install, modify or redistribute this NVIDIA
software.

In consideration of your agreement to abide by the following terms, and
subject to these terms, NVIDIA grants you a personal, non-exclusive
license, under NVIDIA's copyrights in this original NVIDIA software (the
"NVIDIA Software"), to use, reproduce, modify and redistribute the
NVIDIA Software, with or without modifications, in source and/or binary
forms; provided that if you redistribute the NVIDIA Software, you must
retain the copyright notice of NVIDIA, this notice and the following
text and disclaimers in all such redistributions of the NVIDIA Software.
Neither the name, trademarks, service marks nor logos of NVIDIA
Corporation may be used to endorse or promote products derived from the
NVIDIA Software without specific prior written permission from NVIDIA.
Except as expressly stated in this notice, no other rights or licenses
express or implied, are granted by NVIDIA herein, including but not
limited to any patent rights that may be infringed by your derivative
works or by other works in which the NVIDIA Software may be
incorporated. No hardware is licensed hereunder. 

THE NVIDIA SOFTWARE IS BEING PROVIDED ON AN "AS IS" BASIS, WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED,
INCLUDING WITHOUT LIMITATION, WARRANTIES OR CONDITIONS OF TITLE,
NON-INFRINGEMENT, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
ITS USE AND OPERATION EITHER ALONE OR IN COMBINATION WITH OTHER
PRODUCTS.

IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT,
INCIDENTAL, EXEMPLARY, CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, LOST PROFITS; PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) OR ARISING IN ANY WAY
OUT OF THE USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF THE
NVIDIA SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT,
TORT (INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF
NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\****************************************************************************/
//
// cpp.c
//

#define _CRT_SECURE_NO_WARNINGS

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "PpContext.h"
#include "PpTokens.h"

namespace glslang {

int TPpContext::InitCPP()
{
    pool = mem_CreatePool(0, 0);

    return 1;
}

// Handle #define
int TPpContext::CPPdefine(TPpToken* ppToken)
{
    MacroSymbol mac;
    Symbol *symb;

    // get macro name
    int token = scanToken(ppToken);
    if (token != PpAtomIdentifier) {
        parseContext.ppError(ppToken->loc, "must be followed by macro name", "#define", "");
        return token;
    }
    if (ppToken->loc.string >= 0) {
        // We are in user code; check for reserved name use:
        parseContext.reservedPpErrorCheck(ppToken->loc, ppToken->name, "#define");
    }

    // save the original atom
    const int defAtom = ppToken->atom;

    // gather parameters to the macro, between (...)
    token = scanToken(ppToken);
    if (token == '(' && ! ppToken->space) {
        int argc = 0;
        int args[maxMacroArgs];
        do {
            token = scanToken(ppToken);
            if (argc == 0 && token == ')') 
                break;
            if (token != PpAtomIdentifier) {
                parseContext.ppError(ppToken->loc, "bad argument", "#define", "");

                return token;
            }
            // check for duplication of parameter name
            bool duplicate = false;
            for (int a = 0; a < argc; ++a) {
                if (args[a] == ppToken->atom) {
                    parseContext.ppError(ppToken->loc, "duplicate macro parameter", "#define", "");
                    duplicate = true;
                    break;
                }
            }
            if (! duplicate) {
                if (argc < maxMacroArgs)
                    args[argc++] = ppToken->atom;
                else
                    parseContext.ppError(ppToken->loc, "too many macro parameters", "#define", "");
            }
            token = scanToken(ppToken);
        } while (token == ',');
        if (token != ')') {            
            parseContext.ppError(ppToken->loc, "missing parenthesis", "#define", "");

            return token;
        }
        mac.argc = argc;
        mac.args = (int*)mem_Alloc(pool, argc * sizeof(int));
        memcpy(mac.args, args, argc * sizeof(int));
        token = scanToken(ppToken);
    }

    // record the definition of the macro
    TSourceLoc defineLoc = ppToken->loc; // because ppToken is going to go to the next line before we report errors
    mac.body = new TokenStream;
    while (token != '\n') {
        RecordToken(mac.body, token, ppToken);
        token = scanToken(ppToken);
        if (token != '\n' && ppToken->space)
            RecordToken(mac.body, ' ', ppToken);
    }

    // check for duplicate definition
    symb = LookUpSymbol(defAtom);
    if (symb) {
        if (! symb->mac.undef) {
            // Already defined -- need to make sure they are identical:
            // "Two replacement lists are identical if and only if the preprocessing tokens in both have the same number,
            // ordering, spelling, and white-space separation, where all white-space separations are considered identical."
            if (symb->mac.argc != mac.argc)
                parseContext.ppError(defineLoc, "Macro redefined; different number of arguments:", "#define", GetAtomString(defAtom));
            else {
                for (int argc = 0; argc < mac.argc; argc++) {
                    if (symb->mac.args[argc] != mac.args[argc])
                        parseContext.ppError(defineLoc, "Macro redefined; different argument names:", "#define", GetAtomString(defAtom));
                }
                RewindTokenStream(symb->mac.body);
                RewindTokenStream(mac.body);
                int newToken;
                do {
                    int oldToken;
                    TPpToken oldPpToken;
                    TPpToken newPpToken;                    
                    oldToken = ReadToken(symb->mac.body, &oldPpToken);
                    newToken = ReadToken(mac.body, &newPpToken);
                    if (oldToken != newToken || oldPpToken != newPpToken) {
                        parseContext.ppError(defineLoc, "Macro redefined; different substitutions:", "#define", GetAtomString(defAtom));
                        break; 
                    }
                } while (newToken > 0);
            }
        }
    } else
        symb = AddSymbol(defAtom);

    delete symb->mac.body;
    symb->mac = mac;

    return '\n';
}

// Handle #undef
int TPpContext::CPPundef(TPpToken* ppToken)
{
    int token = scanToken(ppToken);
    Symbol *symb;
    if (token != PpAtomIdentifier) {
        parseContext.ppError(ppToken->loc, "must be followed by macro name", "#undef", "");

        return token;
    }

    parseContext.reservedPpErrorCheck(ppToken->loc, ppToken->name, "#undef");

    symb = LookUpSymbol(ppToken->atom);
    if (symb) {
        symb->mac.undef = 1;
    }
    token = scanToken(ppToken);
    if (token != '\n')
        parseContext.ppError(ppToken->loc, "can only be followed by a single macro name", "#undef", "");

    return token;
}

// Handle #else
/* Skip forward to appropriate spot.  This is used both
** to skip to a #endif after seeing an #else, AND to skip to a #else,
** #elif, or #endif after a #if/#ifdef/#ifndef/#elif test was false.
*/
int TPpContext::CPPelse(int matchelse, TPpToken* ppToken)
{
    int atom;
    int depth = 0;
    int token = scanToken(ppToken);

    while (token != EOF) {
        if (token != '#') {
            while (token != '\n' && token != EOF)
                token = scanToken(ppToken);
            
            if (token == EOF)
                return EOF;

            token = scanToken(ppToken);
            continue;
        }

        if ((token = scanToken(ppToken)) != PpAtomIdentifier)
            continue;

        atom = ppToken->atom;
        if (atom == PpAtomIf || atom == PpAtomIfdef || atom == PpAtomIfndef) {
            depth++; 
            ifdepth++; 
            elsetracker++;
        } else if (atom == PpAtomEndif) {
            token = extraTokenCheck(atom, ppToken, scanToken(ppToken));
            elseSeen[elsetracker] = false;
            --elsetracker;
            if (depth == 0) {
                // found the #endif we are looking for
                if (ifdepth) 
                    --ifdepth;
                break;
            }
            --depth;
            --ifdepth;
        } else if (matchelse && depth == 0) {
            if (atom == PpAtomElse) {
                elseSeen[elsetracker] = true;
                token = extraTokenCheck(atom, ppToken, scanToken(ppToken));
                // found the #else we are looking for
                break;
            } else if (atom == PpAtomElif) {
                if (elseSeen[elsetracker])
                    parseContext.ppError(ppToken->loc, "#elif after #else", "#elif", "");
                /* we decrement ifdepth here, because CPPif will increment
                * it and we really want to leave it alone */
                if (ifdepth) {
                    --ifdepth;
                    elseSeen[elsetracker] = false;
                    --elsetracker;
                }

                return CPPif(ppToken);
            }
        } else if (atom == PpAtomElse) {
            if (elseSeen[elsetracker])
                parseContext.ppError(ppToken->loc, "#else after #else", "#else", "");
            else
                elseSeen[elsetracker] = true;
            token = extraTokenCheck(atom, ppToken, scanToken(ppToken));
        } else if (atom == PpAtomElif) {
            if (elseSeen[elsetracker])
                parseContext.ppError(ppToken->loc, "#elif after #else", "#elif", "");
        }
    }

    return token;
}

// Call when there should be no more tokens left on a line.
int TPpContext::extraTokenCheck(int atom, TPpToken* ppToken, int token)
{
    if (token != '\n') {
        static const char* message = "unexpected tokens following directive";

        const char* label;
        if (atom == PpAtomElse)
            label = "#else";
        else if (atom == PpAtomElif)
            label = "#elif";
        else if (atom == PpAtomEndif)
            label = "#endif";
        else if (atom == PpAtomIf)
            label = "#if";
        else if (atom == PpAtomLine)
            label = "#line";
        else
            label = "";

        if (parseContext.relaxedErrors())
            parseContext.ppWarn(ppToken->loc, message, label, "");
        else
            parseContext.ppError(ppToken->loc, message, label, "");

        while (token != '\n')
            token = scanToken(ppToken);
    }

    return token;
}

enum eval_prec {
    MIN_PRECEDENCE,
    COND, LOGOR, LOGAND, OR, XOR, AND, EQUAL, RELATION, SHIFT, ADD, MUL, UNARY,
    MAX_PRECEDENCE
};

namespace {

    int op_logor(int a, int b) { return a || b; }
    int op_logand(int a, int b) { return a && b; }
    int op_or(int a, int b) { return a | b; }
    int op_xor(int a, int b) { return a ^ b; }
    int op_and(int a, int b) { return a & b; }
    int op_eq(int a, int b) { return a == b; }
    int op_ne(int a, int b) { return a != b; }
    int op_ge(int a, int b) { return a >= b; }
    int op_le(int a, int b) { return a <= b; }
    int op_gt(int a, int b) { return a > b; }
    int op_lt(int a, int b) { return a < b; }
    int op_shl(int a, int b) { return a << b; }
    int op_shr(int a, int b) { return a >> b; }
    int op_add(int a, int b) { return a + b; }
    int op_sub(int a, int b) { return a - b; }
    int op_mul(int a, int b) { return a * b; }
    int op_div(int a, int b) { return a / b; }
    int op_mod(int a, int b) { return a % b; }
    int op_pos(int a) { return a; }
    int op_neg(int a) { return -a; }
    int op_cmpl(int a) { return ~a; }
    int op_not(int a) { return !a; }

};

struct TBinop {
    int token, precedence, (*op)(int, int);
} binop[] = {
    { PpAtomOr, LOGOR, op_logor },
    { PpAtomAnd, LOGAND, op_logand },
    { '|', OR, op_or },
    { '^', XOR, op_xor },
    { '&', AND, op_and },
    { PpAtomEQ, EQUAL, op_eq },
    { PpAtomNE, EQUAL, op_ne },
    { '>', RELATION, op_gt },
    { PpAtomGE, RELATION, op_ge },
    { '<', RELATION, op_lt },
    { PpAtomLE, RELATION, op_le },
    { PpAtomLeft, SHIFT, op_shl },
    { PpAtomRight, SHIFT, op_shr },
    { '+', ADD, op_add },
    { '-', ADD, op_sub },
    { '*', MUL, op_mul },
    { '/', MUL, op_div },
    { '%', MUL, op_mod },
};

struct TUnop {
    int token, (*op)(int);
} unop[] = {
    { '+', op_pos },
    { '-', op_neg },
    { '~', op_cmpl },
    { '!', op_not },
};

#define NUM_ELEMENTS(A) (sizeof(A) / sizeof(A[0]))

int TPpContext::eval(int token, int precedence, bool shortCircuit, int& res, bool& err, TPpToken* ppToken)
{
    TSourceLoc loc = ppToken->loc;  // because we sometimes read the newline before reporting the error
    if (token == PpAtomIdentifier) {
        if (ppToken->atom == PpAtomDefined) {
            bool needclose = 0;
            token = scanToken(ppToken);
            if (token == '(') {
                needclose = true;
                token = scanToken(ppToken);
            }
            if (token != PpAtomIdentifier) {
                parseContext.ppError(loc, "incorrect directive, expected identifier", "preprocessor evaluation", "");
                err = true;
                res = 0;

                return token;
            }
            Symbol* s = LookUpSymbol(ppToken->atom);
            res = s ? ! s->mac.undef : 0;
            token = scanToken(ppToken);
            if (needclose) {
                if (token != ')') {
                    parseContext.ppError(loc, "expected ')'", "preprocessor evaluation", "");
                    err = true;
                    res = 0;

                    return token;
                }
                token = scanToken(ppToken);
            }
        } else {
            token = evalToToken(token, shortCircuit, res, err, ppToken);
            return eval(token, precedence, shortCircuit, res, err, ppToken);
        }
    } else if (token == PpAtomConstInt) {
        res = ppToken->ival;
        token = scanToken(ppToken);
    } else if (token == '(') {
        token = scanToken(ppToken);
        token = eval(token, MIN_PRECEDENCE, shortCircuit, res, err, ppToken);
        if (! err) {
            if (token != ')') {
                parseContext.ppError(loc, "expected ')'", "preprocessor evaluation", "");
                err = true;
                res = 0;

                return token;
            }
            token = scanToken(ppToken);
        }
    } else {
        int op;
        for (op = NUM_ELEMENTS(unop) - 1; op >= 0; op--) {
            if (unop[op].token == token)
                break;
        }
        if (op >= 0) {
            token = scanToken(ppToken);
            token = eval(token, UNARY, shortCircuit, res, err, ppToken);
            res = unop[op].op(res);
        } else {
            parseContext.ppError(loc, "bad expression", "preprocessor evaluation", "");
            err = true;
            res = 0;

            return token;
        }
    }

    token = evalToToken(token, shortCircuit, res, err, ppToken);

    // Perform evaluation of binary operation, if there is one, otherwise we are done.
    while (! err) {
        if (token == ')' || token == '\n') 
            break;
        int op;
        for (op = NUM_ELEMENTS(binop) - 1; op >= 0; op--) {
            if (binop[op].token == token)
                break;
        }
        if (op < 0 || binop[op].precedence <= precedence)
            break;
        int leftSide = res;
        
        // Setup short-circuiting, needed for ES, unless already in a short circuit.
        // (Once in a short-circuit, can't turn off again, until that whole subexpression is done.
        if (! shortCircuit) {
            if ((token == PpAtomOr  && leftSide == 1) ||
                (token == PpAtomAnd && leftSide == 0))
                shortCircuit = true;
        }

        token = scanToken(ppToken);
        token = eval(token, binop[op].precedence, shortCircuit, res, err, ppToken);

        if (binop[op].op == op_div || binop[op].op == op_mod) {
            if (res == 0) {
                parseContext.ppError(loc, "division by 0", "preprocessor evaluation", "");
                res = 1;
            }
        }
        res = binop[op].op(leftSide, res);
    }

    return token;
}

// Expand macros, skipping empty expansions, to get to the first real token in those expansions.
int TPpContext::evalToToken(int token, bool shortCircuit, int& res, bool& err, TPpToken* ppToken)
{
    while (token == PpAtomIdentifier && ppToken->atom != PpAtomDefined) {
        int macroReturn = MacroExpand(ppToken->atom, ppToken, true, false);
        if (macroReturn == 0) {
            parseContext.ppError(ppToken->loc, "can't evaluate expression", "preprocessor evaluation", "");
            err = true;
            res = 0;
            token = scanToken(ppToken);
            break;
        }
        if (macroReturn == -1) {
            if (! shortCircuit && parseContext.profile == EEsProfile) {
                const char* message = "undefined macro in expression not allowed in es profile";
                if (parseContext.relaxedErrors())
                    parseContext.ppWarn(ppToken->loc, message, "preprocessor evaluation", ppToken->name);
                else
                    parseContext.ppError(ppToken->loc, message, "preprocessor evaluation", ppToken->name);
            }
        }
        token = scanToken(ppToken);
    }

    return token;
}

// Handle #if
int TPpContext::CPPif(TPpToken* ppToken) 
{
    int token = scanToken(ppToken);
    elsetracker++;
    if (! ifdepth++)
        ifloc = ppToken->loc;
    if (ifdepth > maxIfNesting) {
        parseContext.ppError(ppToken->loc, "maximum nesting depth exceeded", "#if", "");
        return 0;
    }
    int res = 0;
    bool err = false;
    token = eval(token, MIN_PRECEDENCE, false, res, err, ppToken);
    token = extraTokenCheck(PpAtomIf, ppToken, token);
    if (!res && !err)
        token = CPPelse(1, ppToken);

    return token;
}

// Handle #ifdef
int TPpContext::CPPifdef(int defined, TPpToken* ppToken)
{
    int token = scanToken(ppToken);
    int name = ppToken->atom;
    if (++ifdepth > maxIfNesting) {
        parseContext.ppError(ppToken->loc, "maximum nesting depth exceeded", "#ifdef", "");
        return 0;
    }
    elsetracker++;
    if (token != PpAtomIdentifier) {
        if (defined)
            parseContext.ppError(ppToken->loc, "must be followed by macro name", "#ifdef", "");
        else 
            parseContext.ppError(ppToken->loc, "must be followed by macro name", "#ifndef", "");
    } else {
        Symbol *s = LookUpSymbol(name);
        token = scanToken(ppToken);
        if (token != '\n') {
            parseContext.ppError(ppToken->loc, "unexpected tokens following #ifdef directive - expected a newline", "#ifdef", "");
            while (token != '\n')
                token = scanToken(ppToken);
        }
        if (((s && !s->mac.undef) ? 1 : 0) != defined)
            token = CPPelse(1, ppToken);
    }

    return token;
}

// Handle #line
int TPpContext::CPPline(TPpToken* ppToken) 
{
    // "#line must have, after macro substitution, one of the following forms:
    // "#line line
    // "#line line source-string-number"

    int token = scanToken(ppToken);
    const int directiveLoc = ppToken->loc.line;
    if (token == '\n') {
        parseContext.ppError(ppToken->loc, "must by followed by an integral literal", "#line", "");
        return token;
    }

    int lineRes = 0; // Line number after macro expansion.
    int lineToken = 0;
    int fileRes = 0; // Source file number after macro expansion.
    bool hasFile = false;
    bool lineErr = false;
    bool fileErr = false;
    token = eval(token, MIN_PRECEDENCE, false, lineRes, lineErr, ppToken);
    if (! lineErr) {
        lineToken = lineRes;
        if (token == '\n')
            ++lineRes;

        if (parseContext.lineDirectiveShouldSetNextLine())
            --lineRes;
        parseContext.setCurrentLine(lineRes);

        if (token != '\n') {
            token = eval(token, MIN_PRECEDENCE, false, fileRes, fileErr, ppToken);
            if (! fileErr)
                parseContext.setCurrentString(fileRes);
                hasFile = true;
        }
    }
    if (!fileErr && !lineErr) {
        parseContext.notifyLineDirective(directiveLoc, lineToken, hasFile, fileRes);
    }
    token = extraTokenCheck(PpAtomLine, ppToken, token);

    return token;
}

// Handle #error
int TPpContext::CPPerror(TPpToken* ppToken) 
{
    int token = scanToken(ppToken);
    std::string message;
    TSourceLoc loc = ppToken->loc;

    while (token != '\n') {
        if (token == PpAtomConstInt || token == PpAtomConstUint ||
            token == PpAtomConstFloat || token == PpAtomConstDouble) {
                message.append(ppToken->name);
        } else if (token == PpAtomIdentifier || token == PpAtomConstString) {
            message.append(ppToken->name);
        } else {
            message.append(GetAtomString(token));
        }
        message.append(" ");
        token = scanToken(ppToken);
    }
    parseContext.notifyErrorDirective(loc.line, message.c_str());
    //store this msg into the shader's information log..set the Compile Error flag!!!!
    parseContext.ppError(loc, message.c_str(), "#error", "");

    return '\n';
}

// Handle #pragma
int TPpContext::CPPpragma(TPpToken* ppToken)
{
    char SrcStrName[2];
    TVector<TString> tokens;

    TSourceLoc loc = ppToken->loc;  // because we go to the next line before processing
    int token = scanToken(ppToken);
    while (token != '\n' && token != EOF) {
        switch (token) {
        case PpAtomIdentifier:
        case PpAtomConstInt:
        case PpAtomConstUint:
        case PpAtomConstFloat:
        case PpAtomConstDouble:
            tokens.push_back(ppToken->name);
            break;
        default:
            SrcStrName[0] = (char)token;
            SrcStrName[1] = '\0';
            tokens.push_back(SrcStrName);
        }
        token = scanToken(ppToken);
    }

    if (token == EOF)
        parseContext.ppError(loc, "directive must end with a newline", "#pragma", "");
    else
        parseContext.handlePragma(loc, tokens);

    return token;    
}

// #version: This is just for error checking: the version and profile are decided before preprocessing starts
int TPpContext::CPPversion(TPpToken* ppToken)
{
    int token = scanToken(ppToken);

    if (errorOnVersion || versionSeen)
        parseContext.ppError(ppToken->loc, "must occur first in shader", "#version", "");
    versionSeen = true;

    if (token == '\n') {
        parseContext.ppError(ppToken->loc, "must be followed by version number", "#version", "");

        return token;
    }

    if (token != PpAtomConstInt)
        parseContext.ppError(ppToken->loc, "must be followed by version number", "#version", "");

    ppToken->ival = atoi(ppToken->name);
    int versionNumber = ppToken->ival;
    int line = ppToken->loc.line;
    token = scanToken(ppToken);

    if (token == '\n') {
        parseContext.notifyVersion(line, versionNumber, nullptr);
        return token;
    } else {
        if (ppToken->atom != PpAtomCore &&
            ppToken->atom != PpAtomCompatibility &&
            ppToken->atom != PpAtomEs)
            parseContext.ppError(ppToken->loc, "bad profile name; use es, core, or compatibility", "#version", "");
        parseContext.notifyVersion(line, versionNumber, ppToken->name);
        token = scanToken(ppToken);

        if (token == '\n')
            return token;
        else
            parseContext.ppError(ppToken->loc, "bad tokens following profile -- expected newline", "#version", "");
    }

    return token;
}

// Handle #extension
int TPpContext::CPPextension(TPpToken* ppToken)
{
    int line = ppToken->loc.line;
    int token = scanToken(ppToken);
    char extensionName[80];

    if (token=='\n') {
        parseContext.ppError(ppToken->loc, "extension name not specified", "#extension", "");
        return token;
    }

    if (token != PpAtomIdentifier)
        parseContext.ppError(ppToken->loc, "extension name expected", "#extension", "");

    strcpy(extensionName, ppToken->name);

    token = scanToken(ppToken);
    if (token != ':') {
        parseContext.ppError(ppToken->loc, "':' missing after extension name", "#extension", "");
        return token;
    }

    token = scanToken(ppToken);
    if (token != PpAtomIdentifier) {
        parseContext.ppError(ppToken->loc, "behavior for extension not specified", "#extension", "");
        return token;
    }

    parseContext.updateExtensionBehavior(line, extensionName, ppToken->name);
    parseContext.notifyExtensionDirective(line, extensionName, ppToken->name);

    token = scanToken(ppToken);
    if (token == '\n')
        return token;
    else
        parseContext.ppError(ppToken->loc,  "extra tokens -- expected newline", "#extension","");

    return token;
}

int TPpContext::readCPPline(TPpToken* ppToken)
{
    int token = scanToken(ppToken);

    if (token == PpAtomIdentifier) {
        switch (ppToken->atom) {
        case PpAtomDefine:
            token = CPPdefine(ppToken);
            break;
        case PpAtomElse:
            if (elsetracker[elseSeen])
                parseContext.ppError(ppToken->loc, "#else after #else", "#else", "");
            elsetracker[elseSeen] = true;
            if (! ifdepth)
                parseContext.ppError(ppToken->loc, "mismatched statements", "#else", "");
            token = extraTokenCheck(PpAtomElse, ppToken, scanToken(ppToken));
            token = CPPelse(0, ppToken);
            break;
        case PpAtomElif:
            if (! ifdepth)
                parseContext.ppError(ppToken->loc, "mismatched statements", "#elif", "");
            if (elseSeen[elsetracker])
                parseContext.ppError(ppToken->loc, "#elif after #else", "#elif", "");
            // this token is really a dont care, but we still need to eat the tokens
            token = scanToken(ppToken); 
            while (token != '\n')
                token = scanToken(ppToken);
            token = CPPelse(0, ppToken);
            break;
        case PpAtomEndif:
            elseSeen[elsetracker] = false;
            --elsetracker;
            if (! ifdepth)
                parseContext.ppError(ppToken->loc, "mismatched statements", "#endif", "");
            else
                --ifdepth;
            token = extraTokenCheck(PpAtomEndif, ppToken, scanToken(ppToken));
            break;
        case PpAtomIf:
            token = CPPif(ppToken);
            break;
        case PpAtomIfdef:
            token = CPPifdef(1, ppToken);
            break;
        case PpAtomIfndef:
            token = CPPifdef(0, ppToken);
            break;
        case PpAtomLine:
            token = CPPline(ppToken);
            break;
        case PpAtomPragma:
            token = CPPpragma(ppToken);
            break;
        case PpAtomUndef:
            token = CPPundef(ppToken);
            break;
        case PpAtomError:
            token = CPPerror(ppToken);
            break;
        case PpAtomVersion:
            token = CPPversion(ppToken);
            break;
        case PpAtomExtension:
            token = CPPextension(ppToken);
            break;
        default:
            parseContext.ppError(ppToken->loc, "invalid directive:", "#", ppToken->name);
            break;
        }
    } else if (token != '\n' && token != EOF)
        parseContext.ppError(ppToken->loc, "invalid directive", "#", "");

    while (token != '\n' && token != 0 && token != EOF)
        token = scanToken(ppToken);

    return token;
}

TPpContext::TokenStream* TPpContext::PrescanMacroArg(TokenStream* a, TPpToken* ppToken, bool newLineOkay)
{
    int token;
    TokenStream *n;
    RewindTokenStream(a);
    do {
        token = ReadToken(a, ppToken);
        if (token == PpAtomIdentifier && LookUpSymbol(ppToken->atom))
            break;
    } while (token != tInput::endOfInput);

    if (token == tInput::endOfInput)
        return a;

    n = new TokenStream;
    pushInput(new tMarkerInput(this));
    pushTokenStreamInput(a);
    while ((token = scanToken(ppToken)) != tMarkerInput::marker) {
        if (token == PpAtomIdentifier && MacroExpand(ppToken->atom, ppToken, false, newLineOkay) != 0)
            continue;
        RecordToken(n, token, ppToken);
    }
    popInput();
    delete a;

    return n;
}

// 
// Return the next token for a macro expansion, handling macro args.
//
int TPpContext::tMacroInput::scan(TPpToken* ppToken)
{
    int token;
    do {
        token = pp->ReadToken(mac->body, ppToken);
    } while (token == ' ');  // handle white space in macro

    // TODO: preprocessor:  properly handle whitespace (or lack of it) between tokens when expanding
    if (token == PpAtomIdentifier) {
        int i;
        for (i = mac->argc - 1; i >= 0; i--)
            if (mac->args[i] == ppToken->atom) 
                break;
        if (i >= 0) {
            pp->pushTokenStreamInput(args[i]);

            return pp->scanToken(ppToken);
        }
    }

    if (token == endOfInput)
        mac->busy = 0;
        
    return token;
}

// return a zero, for scanning a macro that was never defined
int TPpContext::tZeroInput::scan(TPpToken* ppToken)
{
    if (done)
        return endOfInput;

    strcpy(ppToken->name, "0");
    ppToken->ival = 0;
    ppToken->space = false;
    done = true;

    return PpAtomConstInt;
}

//
// Check an identifier (atom) to see if it is a macro that should be expanded.
// If it is, and defined, push a tInput that will produce the appropriate expansion
// and return 1.
// If it is, but undefined, and expandUndef is requested, push a tInput that will 
// expand to 0 and return -1.
// Otherwise, return 0 to indicate no expansion, which is not necessarily an error.
//
int TPpContext::MacroExpand(int atom, TPpToken* ppToken, bool expandUndef, bool newLineOkay)
{
    ppToken->space = false;
    switch (atom) {
    case PpAtomLineMacro:
        ppToken->ival = parseContext.getCurrentLoc().line;
        sprintf(ppToken->name, "%d", ppToken->ival);
        UngetToken(PpAtomConstInt, ppToken);
        return 1;

    case PpAtomFileMacro:
        ppToken->ival = parseContext.getCurrentLoc().string;
        sprintf(ppToken->name, "%d", ppToken->ival);
        UngetToken(PpAtomConstInt, ppToken);
        return 1;

    case PpAtomVersionMacro:
        ppToken->ival = parseContext.version;
        sprintf(ppToken->name, "%d", ppToken->ival);
        UngetToken(PpAtomConstInt, ppToken);
        return 1;

    default:
        break;
    }

    Symbol *sym = LookUpSymbol(atom);
    int token;
    int depth = 0;

    // no recursive expansions
    if (sym && sym->mac.busy)
        return 0;

    // not expanding undefined macros
    if ((! sym || sym->mac.undef) && ! expandUndef)
        return 0;

    // 0 is the value of an undefined macro
    if ((! sym || sym->mac.undef) && expandUndef) {
        pushInput(new tZeroInput(this));
        return -1;
    }

    tMacroInput *in = new tMacroInput(this);

    TSourceLoc loc = ppToken->loc;  // in case we go to the next line before discovering the error
    in->mac = &sym->mac;
    if (sym->mac.args) {
        token = scanToken(ppToken);
        if (newLineOkay) {
            while (token == '\n')                
                token = scanToken(ppToken);
        }
        if (token != '(') {
            parseContext.ppError(loc, "expected '(' following", "macro expansion", GetAtomString(atom));
            UngetToken(token, ppToken);
            ppToken->atom = atom;

            delete in;
            return 0;
        }
        in->args.resize(in->mac->argc);
        for (int i = 0; i < in->mac->argc; i++)
            in->args[i] = new TokenStream;
        int arg = 0;
        bool tokenRecorded = false;
        do {
            depth = 0;
            while (1) {
                token = scanToken(ppToken);
                if (token == EOF) {
                    parseContext.ppError(loc, "EOF in macro", "macro expansion", GetAtomString(atom));
                    delete in;
                    return 0;
                }
                if (token == '\n') {
                    if (! newLineOkay) {
                        parseContext.ppError(loc, "end of line in macro substitution:", "macro expansion", GetAtomString(atom));
                        delete in;
                        return 0;
                    }
                    continue;
                }
                if (token == '#') {
                    parseContext.ppError(ppToken->loc, "unexpected '#'", "macro expansion", GetAtomString(atom));
                    delete in;
                    return 0;
                }
                if (in->mac->argc == 0 && token != ')')
                    break;
                if (depth == 0 && (token == ',' || token == ')'))
                    break;
                if (token == '(')
                    depth++;
                if (token == ')')
                    depth--;
                RecordToken(in->args[arg], token, ppToken);
                tokenRecorded = true;
            }
            if (token == ')') {
                if (in->mac->argc == 1 && tokenRecorded == 0)
                    break;
                arg++;
                break;
            }
            arg++;
        } while (arg < in->mac->argc);

        if (arg < in->mac->argc)
            parseContext.ppError(loc, "Too few args in Macro", "macro expansion", GetAtomString(atom));
        else if (token != ')') {
            depth=0;
            while (token != EOF && (depth > 0 || token != ')')) {
                if (token == ')')
                    depth--;
                token = scanToken(ppToken);
                if (token == '(')
                    depth++;
            }

            if (token == EOF) {
                parseContext.ppError(loc, "EOF in macro", "macro expansion", GetAtomString(atom));
                delete in;
                return 0;
            }
            parseContext.ppError(loc, "Too many args in macro", "macro expansion", GetAtomString(atom));
        }
        for (int i = 0; i < in->mac->argc; i++)
            in->args[i] = PrescanMacroArg(in->args[i], ppToken, newLineOkay);
    }

    pushInput(in);
    sym->mac.busy = 1;
    RewindTokenStream(sym->mac.body);

    return 1;
}

} // end namespace glslang
