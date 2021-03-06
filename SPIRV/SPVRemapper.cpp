//
//Copyright (C) 2015 LunarG, Inc.
//
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

#include "SPVRemapper.h"
#include "doc.h"

#if !defined (use_cpp11)
// ... not supported before C++11
#else // defined (use_cpp11)

#include <algorithm>
#include <cassert>

namespace spv {

    // By default, just abort on error.  Can be overridden via RegisterErrorHandler
    spirvbin_t::errorfn_t spirvbin_t::errorHandler = [](const std::string&) { exit(5); };
    // By default, eat log messages.  Can be overridden via RegisterLogHandler
    spirvbin_t::logfn_t   spirvbin_t::logHandler   = [](const std::string&) { };

    // This can be overridden to provide other message behavior if needed
    void spirvbin_t::msg(int minVerbosity, int indent, const std::string& txt) const
    {
        if (verbose >= minVerbosity)
            logHandler(std::string(indent, ' ') + txt);
    }

    // hash opcode, with special handling for OpExtInst
    std::uint32_t spirvbin_t::asOpCodeHash(unsigned word)
    {
        const spv::Op opCode = asOpCode(word);

        std::uint32_t offset = 0;

        switch (opCode) {
        case spv::OpExtInst:
            offset += asId(word + 4); break;
        default:
            break;
        }

        return opCode * 19 + offset; // 19 = small prime
    }

    spirvbin_t::range_t spirvbin_t::literalRange(spv::Op opCode) const
    {
        static const int maxCount = 1<<30;

        switch (opCode) {
        case spv::OpTypeFloat:        // fall through...
        case spv::OpTypePointer:      return range_t(2, 3);
        case spv::OpTypeInt:          return range_t(2, 4);
        case spv::OpTypeSampler:      return range_t(3, 8);
        case spv::OpTypeVector:       // fall through
        case spv::OpTypeMatrix:       // ...
        case spv::OpTypePipe:         return range_t(3, 4);
        case spv::OpConstant:         return range_t(3, maxCount);
        default:                      return range_t(0, 0);
        }
    }

    spirvbin_t::range_t spirvbin_t::typeRange(spv::Op opCode) const
    {
        static const int maxCount = 1<<30;

        if (isConstOp(opCode))
            return range_t(1, 2);

        switch (opCode) {
        case spv::OpTypeVector:       // fall through
        case spv::OpTypeMatrix:       // ... 
        case spv::OpTypeSampler:      // ... 
        case spv::OpTypeArray:        // ... 
        case spv::OpTypeRuntimeArray: // ... 
        case spv::OpTypePipe:         return range_t(2, 3);
        case spv::OpTypeStruct:       // fall through
        case spv::OpTypeFunction:     return range_t(2, maxCount);
        case spv::OpTypePointer:      return range_t(3, 4);
        default:                      return range_t(0, 0);
        }
    }

    spirvbin_t::range_t spirvbin_t::constRange(spv::Op opCode) const
    {
        static const int maxCount = 1<<30;

        switch (opCode) {
        case spv::OpTypeArray:         // fall through...
        case spv::OpTypeRuntimeArray:  return range_t(3, 4);
        case spv::OpConstantComposite: return range_t(3, maxCount);
        default:                       return range_t(0, 0);
        }
    }

    // Is this an opcode we should remove when using --strip?
    bool spirvbin_t::isStripOp(spv::Op opCode) const
    {
        switch (opCode) {
        case spv::OpSource:
        case spv::OpSourceExtension:
        case spv::OpName:
        case spv::OpMemberName:
        case spv::OpLine:           return true;
        default:                    return false;
        }
    }

    bool spirvbin_t::isFlowCtrlOpen(spv::Op opCode) const
    {
        switch (opCode) {
        case spv::OpBranchConditional:
        case spv::OpSwitch:         return true;
        default:                    return false;
        }
    }

    bool spirvbin_t::isFlowCtrlClose(spv::Op opCode) const
    {
        switch (opCode) {
        case spv::OpLoopMerge:
        case spv::OpSelectionMerge: return true;
        default:                    return false;
        }
    }

    bool spirvbin_t::isTypeOp(spv::Op opCode) const
    {
        switch (opCode) {
        case spv::OpTypeVoid:
        case spv::OpTypeBool:
        case spv::OpTypeInt:
        case spv::OpTypeFloat:
        case spv::OpTypeVector:
        case spv::OpTypeMatrix:
        case spv::OpTypeSampler:
        case spv::OpTypeFilter:
        case spv::OpTypeArray:
        case spv::OpTypeRuntimeArray:
        case spv::OpTypeStruct:
        case spv::OpTypeOpaque:
        case spv::OpTypePointer:
        case spv::OpTypeFunction:
        case spv::OpTypeEvent:
        case spv::OpTypeDeviceEvent:
        case spv::OpTypeReserveId:
        case spv::OpTypeQueue:
        case spv::OpTypePipe:         return true;
        default:                      return false;
        }
    }

    bool spirvbin_t::isConstOp(spv::Op opCode) const
    {
        switch (opCode) {
        case spv::OpConstantNullObject: error("unimplemented constant type");
        case spv::OpConstantSampler:    error("unimplemented constant type");

        case spv::OpConstantTrue:
        case spv::OpConstantFalse:
        case spv::OpConstantNullPointer:
        case spv::OpConstantComposite:
        case spv::OpConstant:         return true;
        default:                      return false;
        }
    }

    const auto inst_fn_nop = [](spv::Op, unsigned) { return false; };
    const auto op_fn_nop   = [](spv::Id&)          { };

    // g++ doesn't like these defined in the class proper in an anonymous namespace.
    // Dunno why.  Also MSVC doesn't like the constexpr keyword.  Also dunno why.
    // Defining them externally seems to please both compilers, so, here they are.
    const spv::Id spirvbin_t::unmapped    = spv::Id(-10000);
    const spv::Id spirvbin_t::unused      = spv::Id(-10001);
    const int     spirvbin_t::header_size = 5;

    spv::Id spirvbin_t::nextUnusedId(spv::Id id)
    {
        while (isNewIdMapped(id))  // search for an unused ID
            ++id;

        return id;
    }

    spv::Id spirvbin_t::localId(spv::Id id, spv::Id newId)
    {
        assert(id != spv::NoResult && newId != spv::NoResult);

        if (id >= idMapL.size())
            idMapL.resize(id+1, unused);

        if (newId != unmapped && newId != unused) {
            if (isOldIdUnused(id))
                error(std::string("ID unused in module: ") + std::to_string(id));

            if (!isOldIdUnmapped(id))
                error(std::string("ID already mapped: ") + std::to_string(id) + " -> "
                + std::to_string(localId(id)));

            if (isNewIdMapped(newId))
                error(std::string("ID already used in module: ") + std::to_string(newId));

            msg(4, 4, std::string("map: ") + std::to_string(id) + " -> " + std::to_string(newId));
            setMapped(newId);
            largestNewId = std::max(largestNewId, newId);
        }

        return idMapL[id] = newId;
    }

    // Parse a literal string from the SPIR binary and return it as an std::string
    // Due to C++11 RValue references, this doesn't copy the result string.
    std::string spirvbin_t::literalString(unsigned word) const
    {
        std::string literal;

        literal.reserve(16);

        const char* bytes = reinterpret_cast<const char*>(spv.data() + word);

        while (bytes && *bytes)
            literal += *bytes++;

        return literal;
    }


    void spirvbin_t::applyMap()
    {
        msg(3, 2, std::string("Applying map: "));

        // Map local IDs through the ID map
        process(inst_fn_nop, // ignore instructions
            [this](spv::Id& id) {
                id = localId(id);
                assert(id != unused && id != unmapped);
            }
        );
    }


    // Find free IDs for anything we haven't mapped
    void spirvbin_t::mapRemainder()
    {
        msg(3, 2, std::string("Remapping remainder: "));

        spv::Id     unusedId  = 1;  // can't use 0: that's NoResult
        spirword_t  maxBound  = 0;

        for (spv::Id id = 0; id < idMapL.size(); ++id) {
            if (isOldIdUnused(id))
                continue;

            // Find a new mapping for any used but unmapped IDs
            if (isOldIdUnmapped(id))
                localId(id, unusedId = nextUnusedId(unusedId));

            if (isOldIdUnmapped(id))
                error(std::string("old ID not mapped: ") + std::to_string(id));

            // Track max bound
            maxBound = std::max(maxBound, localId(id) + 1);
        }

        bound(maxBound); // reset header ID bound to as big as it now needs to be
    }

    void spirvbin_t::stripDebug()
    {
        if ((options & STRIP) == 0)
            return;

        // build local Id and name maps
        process(
            [&](spv::Op opCode, unsigned start) {
                // remember opcodes we want to strip later
                if (isStripOp(opCode))
                    stripInst(start);
                return true;
            },
            op_fn_nop);
    }

    void spirvbin_t::buildLocalMaps()
    {
        msg(2, 2, std::string("build local maps: "));

        mapped.clear();
        idMapL.clear();
//      preserve nameMap, so we don't clear that.
        fnPos.clear();
        fnPosDCE.clear();
        fnCalls.clear();
        typeConstPos.clear();
        typeConstPosR.clear();
        entryPoint = spv::NoResult;
        largestNewId = 0;

        idMapL.resize(bound(), unused);

        int         fnStart = 0;
        spv::Id     fnRes   = spv::NoResult;

        // build local Id and name maps
        process(
            [&](spv::Op opCode, unsigned start) {
                // remember opcodes we want to strip later
                if ((options & STRIP) && isStripOp(opCode))
                    stripInst(start);

                if (opCode == spv::Op::OpName) {
                    const spv::Id    target = asId(start+1);
                    const std::string  name = literalString(start+2);
                    nameMap[name] = target;

                } else if (opCode == spv::Op::OpFunctionCall) {
                    ++fnCalls[asId(start + 3)];
                } else if (opCode == spv::Op::OpEntryPoint) {
                    entryPoint = asId(start + 2);
                } else if (opCode == spv::Op::OpFunction) {
                    if (fnStart != 0)
                        error("nested function found");
                    fnStart = start;
                    fnRes   = asId(start + 2);
                } else if (opCode == spv::Op::OpFunctionEnd) {
                    assert(fnRes != spv::NoResult);
                    if (fnStart == 0)
                        error("function end without function start");
                    fnPos[fnRes] = range_t(fnStart, start + asWordCount(start));
                    fnStart = 0;
                } else if (isConstOp(opCode)) {
                    assert(asId(start + 2) != spv::NoResult);
                    typeConstPos.insert(start);
                    typeConstPosR[asId(start + 2)] = start;
                } else if (isTypeOp(opCode)) {
                    assert(asId(start + 1) != spv::NoResult);
                    typeConstPos.insert(start);
                    typeConstPosR[asId(start + 1)] = start;
                }

                return false;
            },

            [this](spv::Id& id) { localId(id, unmapped); }
        );
    }

    // Validate the SPIR header
    void spirvbin_t::validate() const
    {
        msg(2, 2, std::string("validating: "));

        if (spv.size() < header_size)
            error("file too short: ");

        if (magic() != spv::MagicNumber)
            error("bad magic number");

        // field 1 = version
        // field 2 = generator magic
        // field 3 = result <id> bound

        if (schemaNum() != 0)
            error("bad schema, must be 0");
    }


    int spirvbin_t::processInstruction(unsigned word, instfn_t instFn, idfn_t idFn)
    {
        const auto     instructionStart = word;
        const unsigned wordCount = asWordCount(instructionStart);
        const spv::Op  opCode    = asOpCode(instructionStart);
        const int      nextInst  = word++ + wordCount;

        if (nextInst > int(spv.size()))
            error("spir instruction terminated too early");

        // Base for computing number of operands; will be updated as more is learned
        unsigned numOperands = wordCount - 1;

        if (instFn(opCode, instructionStart))
            return nextInst;

        // Read type and result ID from instruction desc table
        if (spv::InstructionDesc[opCode].hasType()) {
            idFn(asId(word++));
            --numOperands;
        }

        if (spv::InstructionDesc[opCode].hasResult()) {
            idFn(asId(word++));
            --numOperands;
        }

        // Extended instructions: currently, assume everything is an ID.
        // TODO: add whatever data we need for exceptions to that
        if (opCode == spv::OpExtInst) {
            word        += 2; // instruction set, and instruction from set
            numOperands -= 2;

            for (unsigned op=0; op < numOperands; ++op)
                idFn(asId(word++)); // ID

            return nextInst;
        }

        // Store IDs from instruction in our map
        for (int op = 0; op < spv::InstructionDesc[opCode].operands.getNum(); ++op, --numOperands) {
            switch (spv::InstructionDesc[opCode].operands.getClass(op)) {
            case spv::OperandId:
                idFn(asId(word++));
                break;

            case spv::OperandOptionalId:
            case spv::OperandVariableIds:
                for (unsigned i = 0; i < numOperands; ++i)
                    idFn(asId(word++));
                return nextInst;

            case spv::OperandVariableLiterals:
                // for clarity
                // if (opCode == spv::OpDecorate && asDecoration(word - 1) == spv::DecorationBuiltIn) {
                //     ++word;
                //     --numOperands;
                // }
                // word += numOperands;
                return nextInst;

            case spv::OperandVariableLiteralId:
                while (numOperands > 0) {
                    ++word;             // immediate
                    idFn(asId(word++)); // ID
                    numOperands -= 2;
                }
                return nextInst;

            case spv::OperandLiteralString:
                // word += literalStringWords(literalString(word)); // for clarity
                return nextInst;

                // Single word operands we simply ignore, as they hold no IDs
            case spv::OperandLiteralNumber:
            case spv::OperandSource:
            case spv::OperandExecutionModel:
            case spv::OperandAddressing:
            case spv::OperandMemory:
            case spv::OperandExecutionMode:
            case spv::OperandStorage:
            case spv::OperandDimensionality:
            case spv::OperandDecoration:
            case spv::OperandBuiltIn:
            case spv::OperandSelect:
            case spv::OperandLoop:
            case spv::OperandFunction:
            case spv::OperandMemorySemantics:
            case spv::OperandMemoryAccess:
            case spv::OperandExecutionScope:
            case spv::OperandGroupOperation:
            case spv::OperandKernelEnqueueFlags:
            case spv::OperandKernelProfilingInfo:
                ++word;
                break;

            default:
                break;
            }
        }

        return nextInst;
    }

    // Make a pass over all the instructions and process them given appropriate functions
    spirvbin_t& spirvbin_t::process(instfn_t instFn, idfn_t idFn, unsigned begin, unsigned end)
    {
        // For efficiency, reserve name map space.  It can grow if needed.
        nameMap.reserve(32);

        // If begin or end == 0, use defaults
        begin = (begin == 0 ? header_size          : begin);
        end   = (end   == 0 ? unsigned(spv.size()) : end);

        // basic parsing and InstructionDesc table borrowed from SpvDisassemble.cpp...
        unsigned nextInst = unsigned(spv.size());

        for (unsigned word = begin; word < end; word = nextInst)
            nextInst = processInstruction(word, instFn, idFn);

        return *this;
    }

    // Apply global name mapping to a single module
    void spirvbin_t::mapNames()
    {
        static const std::uint32_t softTypeIdLimit = 3011;  // small prime.  TODO: get from options
        static const std::uint32_t firstMappedID   = 3019;  // offset into ID space

        for (const auto& name : nameMap) {
            std::uint32_t hashval = 1911;
            for (const char c : name.first)
                hashval = hashval * 1009 + c;

            if (isOldIdUnmapped(name.second))
                localId(name.second, nextUnusedId(hashval % softTypeIdLimit + firstMappedID));
        }
    }

    // Map fn contents to IDs of similar functions in other modules
    void spirvbin_t::mapFnBodies()
    {
        static const std::uint32_t softTypeIdLimit = 19071;  // small prime.  TODO: get from options
        static const std::uint32_t firstMappedID   =  6203;  // offset into ID space

        // Initial approach: go through some high priority opcodes first and assign them
        // hash values.

        spv::Id               fnId       = spv::NoResult;
        std::vector<unsigned> instPos;
        instPos.reserve(unsigned(spv.size()) / 16); // initial estimate; can grow if needed.

        // Build local table of instruction start positions
        process(
            [&](spv::Op, unsigned start) { instPos.push_back(start); return true; },
            op_fn_nop);

        // Window size for context-sensitive canonicalization values
        // Emperical best size from a single data set.  TODO: Would be a good tunable.
        // We essentially performa a little convolution around each instruction,
        // to capture the flavor of nearby code, to hopefully match to similar
        // code in other modules.
        static const unsigned windowSize = 2;

        for (unsigned entry = 0; entry < unsigned(instPos.size()); ++entry) {
            const unsigned start  = instPos[entry];
            const spv::Op  opCode = asOpCode(start);

            if (opCode == spv::OpFunction)
                fnId   = asId(start + 2);

            if (opCode == spv::OpFunctionEnd)
                fnId = spv::NoResult;

            if (fnId != spv::NoResult) { // if inside a function
                if (spv::InstructionDesc[opCode].hasResult()) {
                    const unsigned word    = start + (spv::InstructionDesc[opCode].hasType() ? 2 : 1);
                    const spv::Id  resId   = asId(word);
                    std::uint32_t  hashval = fnId * 17; // small prime

                    for (unsigned i = entry-1; i >= entry-windowSize; --i) {
                        if (asOpCode(instPos[i]) == spv::OpFunction)
                            break;
                        hashval = hashval * 30103 + asOpCodeHash(instPos[i]); // 30103 = semiarbitrary prime
                    }

                    for (unsigned i = entry; i <= entry + windowSize; ++i) {
                        if (asOpCode(instPos[i]) == spv::OpFunctionEnd)
                            break;
                        hashval = hashval * 30103 + asOpCodeHash(instPos[i]); // 30103 = semiarbitrary prime
                    }

                    if (isOldIdUnmapped(resId))
                        localId(resId, nextUnusedId(hashval % softTypeIdLimit + firstMappedID));
                }
            }
        }

        spv::Op          thisOpCode(spv::OpNop);
        std::unordered_map<int, int> opCounter;
        int              idCounter(0);
        fnId = spv::NoResult;

        process(
            [&](spv::Op opCode, unsigned start) {
                switch (opCode) {
                case spv::OpFunction:
                    // Reset counters at each function
                    idCounter = 0;
                    opCounter.clear();
                    fnId = asId(start + 2);
                    break;

                case spv::OpTextureSample:
                case spv::OpTextureSampleDref:
                case spv::OpTextureSampleLod:
                case spv::OpTextureSampleProj:
                case spv::OpTextureSampleGrad:
                case spv::OpTextureSampleOffset:
                case spv::OpTextureSampleProjLod:
                case spv::OpTextureSampleProjGrad:
                case spv::OpTextureSampleLodOffset:
                case spv::OpTextureSampleProjOffset:
                case spv::OpTextureSampleGradOffset:                     
                case spv::OpTextureSampleProjLodOffset:
                case spv::OpTextureSampleProjGradOffset:
                case spv::OpDot:
                case spv::OpCompositeExtract:
                case spv::OpCompositeInsert:
                case spv::OpVectorShuffle:
                case spv::OpLabel:
                case spv::OpVariable:

                case spv::OpAccessChain:
                case spv::OpLoad:
                case spv::OpStore:
                case spv::OpCompositeConstruct:
                case spv::OpFunctionCall:
                    ++opCounter[opCode];
                    idCounter = 0;
                    thisOpCode = opCode;
                    break;
                default:
                    thisOpCode = spv::OpNop;
                }

                return false;
            },

            [&](spv::Id& id) {
                if (thisOpCode != spv::OpNop) {
                    ++idCounter;
                    const std::uint32_t hashval = opCounter[thisOpCode] * thisOpCode * 50047 + idCounter + fnId * 117;

                    if (isOldIdUnmapped(id))
                        localId(id, nextUnusedId(hashval % softTypeIdLimit + firstMappedID));
                }
            });
    }

    // EXPERIMENTAL: forward IO and uniform load/stores into operands
    // This produces invalid Schema-0 SPIRV
    void spirvbin_t::forwardLoadStores()
    {
        idset_t fnLocalVars; // set of function local vars
        idmap_t idMap;       // Map of load result IDs to what they load

        // EXPERIMENTAL: Forward input and access chain loads into consumptions
        process(
            [&](spv::Op opCode, unsigned start) {
                // Add inputs and uniforms to the map
                if (((opCode == spv::OpVariable && asWordCount(start) == 4) || (opCode == spv::OpVariableArray)) &&
                    (spv[start+3] == spv::StorageClassUniform ||
                    spv[start+3] == spv::StorageClassUniformConstant ||
                    spv[start+3] == spv::StorageClassInput))
                    fnLocalVars.insert(asId(start+2));

                if (opCode == spv::OpAccessChain && fnLocalVars.count(asId(start+3)) > 0)
                    fnLocalVars.insert(asId(start+2));

                if (opCode == spv::OpLoad && fnLocalVars.count(asId(start+3)) > 0) {
                    idMap[asId(start+2)] = asId(start+3);
                    stripInst(start);
                }

                return false;
            },

            [&](spv::Id& id) { if (idMap.find(id) != idMap.end()) id = idMap[id]; }
        );

        // EXPERIMENTAL: Implicit output stores
        fnLocalVars.clear();
        idMap.clear();

        process(
            [&](spv::Op opCode, unsigned start) {
                // Add inputs and uniforms to the map
                if (((opCode == spv::OpVariable && asWordCount(start) == 4) || (opCode == spv::OpVariableArray)) &&
                    (spv[start+3] == spv::StorageClassOutput))
                    fnLocalVars.insert(asId(start+2));

                if (opCode == spv::OpStore && fnLocalVars.count(asId(start+1)) > 0) {
                    idMap[asId(start+2)] = asId(start+1);
                    stripInst(start);
                }

                return false;
            },
            op_fn_nop);

        process(
            inst_fn_nop,
            [&](spv::Id& id) { if (idMap.find(id) != idMap.end()) id = idMap[id]; }
        );

        strip();          // strip out data we decided to eliminate
    }

    // remove bodies of uncalled functions
    void spirvbin_t::optLoadStore()
    {
        idset_t fnLocalVars;
        // Map of load result IDs to what they load
        idmap_t idMap;

        // Find all the function local pointers stored at most once, and not via access chains
        process(
            [&](spv::Op opCode, unsigned start) {
                const int wordCount = asWordCount(start);

                // Add local variables to the map
                if ((opCode == spv::OpVariable && spv[start+3] == spv::StorageClassFunction && asWordCount(start) == 4) ||
                    (opCode == spv::OpVariableArray && spv[start+3] == spv::StorageClassFunction))
                    fnLocalVars.insert(asId(start+2));

                // Ignore process vars referenced via access chain
                if ((opCode == spv::OpAccessChain || opCode == spv::OpInBoundsAccessChain) && fnLocalVars.count(asId(start+3)) > 0) {
                    fnLocalVars.erase(asId(start+3));
                    idMap.erase(asId(start+3));
                }

                if (opCode == spv::OpLoad && fnLocalVars.count(asId(start+3)) > 0) {
                    // Avoid loads before stores (TODO: why?  Crashes driver, but seems like it shouldn't).
                    if (idMap.find(asId(start+3)) == idMap.end()) {
                        fnLocalVars.erase(asId(start+3));
                        idMap.erase(asId(start+3));
                    }

                    // don't do for volatile references
                    if (wordCount > 4 && (spv[start+4] & spv::MemoryAccessVolatileMask)) {
                        fnLocalVars.erase(asId(start+3));
                        idMap.erase(asId(start+3));
                    }
                }

                if (opCode == spv::OpStore && fnLocalVars.count(asId(start+1)) > 0) {
                    if (idMap.find(asId(start+1)) == idMap.end()) {
                        idMap[asId(start+1)] = asId(start+2);
                    } else {
                        // Remove if it has more than one store to the same pointer
                        fnLocalVars.erase(asId(start+1));
                        idMap.erase(asId(start+1));
                    }

                    // don't do for volatile references
                    if (wordCount > 3 && (spv[start+3] & spv::MemoryAccessVolatileMask)) {
                        fnLocalVars.erase(asId(start+3));
                        idMap.erase(asId(start+3));
                    }
                }

                return true;
            },
            op_fn_nop);

        process(
            [&](spv::Op opCode, unsigned start) {
                if (opCode == spv::OpLoad && fnLocalVars.count(asId(start+3)) > 0)
                    idMap[asId(start+2)] = idMap[asId(start+3)];
                return false;
            },
            op_fn_nop);

        // Remove the load/store/variables for the ones we've discovered
        process(
            [&](spv::Op opCode, unsigned start) {
                if ((opCode == spv::OpLoad  && fnLocalVars.count(asId(start+3)) > 0) ||
                    (opCode == spv::OpStore && fnLocalVars.count(asId(start+1)) > 0) ||
                    (opCode == spv::OpVariable && fnLocalVars.count(asId(start+2)) > 0)) {
                    stripInst(start);
                    return true;
                }

                return false;
            },

            [&](spv::Id& id) { if (idMap.find(id) != idMap.end()) id = idMap[id]; }
        );

        strip();          // strip out data we decided to eliminate
    }

    // remove bodies of uncalled functions
    void spirvbin_t::dceFuncs()
    {
        msg(3, 2, std::string("Removing Dead Functions: "));

        // TODO: There are more efficient ways to do this.
        bool changed = true;

        while (changed) {
            changed = false;

            for (auto fn = fnPos.begin(); fn != fnPos.end(); ) {
                if (fn->first == entryPoint) { // don't DCE away the entry point!
                    ++fn;
                    continue;
                }

                const auto call_it = fnCalls.find(fn->first);

                if (call_it == fnCalls.end() || call_it->second == 0) {
                    changed = true;
                    stripRange.push_back(fn->second);
                    fnPosDCE.insert(*fn);

                    // decrease counts of called functions
                    process(
                        [&](spv::Op opCode, unsigned start) {
                            if (opCode == spv::Op::OpFunctionCall) {
                                const auto call_it = fnCalls.find(asId(start + 3));
                                if (call_it != fnCalls.end()) {
                                    if (--call_it->second <= 0)
                                        fnCalls.erase(call_it);
                                }
                            }

                            return true;
                        },
                        op_fn_nop,
                        fn->second.first,
                        fn->second.second);

                    fn = fnPos.erase(fn);
                } else ++fn;
            }
        }
    }

    // remove unused function variables + decorations
    void spirvbin_t::dceVars()
    {
        msg(3, 2, std::string("DCE Vars: "));

        std::unordered_map<spv::Id, int> varUseCount;

        // Count function variable use
        process(
            [&](spv::Op opCode, unsigned start) {
                if (opCode == spv::OpVariable) { ++varUseCount[asId(start+2)]; return true; }
                return false;
            },

            [&](spv::Id& id) { if (varUseCount[id]) ++varUseCount[id]; }
        );

        // Remove single-use function variables + associated decorations and names
        process(
            [&](spv::Op opCode, unsigned start) {
                if ((opCode == spv::OpVariable && varUseCount[asId(start+2)] == 1)  ||
                    (opCode == spv::OpDecorate && varUseCount[asId(start+1)] == 1)  ||
                    (opCode == spv::OpName     && varUseCount[asId(start+1)] == 1)) {
                        stripInst(start);
                }
                return true;
            },
            op_fn_nop);
    }

    // remove unused types
    void spirvbin_t::dceTypes()
    {
        std::vector<bool> isType(bound(), false);

        // for speed, make O(1) way to get to type query (map is log(n))
        for (const auto typeStart : typeConstPos)
            isType[asTypeConstId(typeStart)] = true;

        std::unordered_map<spv::Id, int> typeUseCount;

        // Count total type usage
        process(inst_fn_nop,
            [&](spv::Id& id) { if (isType[id]) ++typeUseCount[id]; }
        );

        // Remove types from deleted code
        for (const auto& fn : fnPosDCE)
            process(inst_fn_nop,
            [&](spv::Id& id) { if (isType[id]) --typeUseCount[id]; },
            fn.second.first, fn.second.second);

        // Remove single reference types
        for (const auto typeStart : typeConstPos) {
            const spv::Id typeId = asTypeConstId(typeStart);
            if (typeUseCount[typeId] == 1) {
                --typeUseCount[typeId];
                stripInst(typeStart);
            }
        }
    }


#ifdef NOTDEF
    bool spirvbin_t::matchType(const spirvbin_t::globaltypes_t& globalTypes, spv::Id lt, spv::Id gt) const
    {
        // Find the local type id "lt" and global type id "gt"
        const auto lt_it = typeConstPosR.find(lt);
        if (lt_it == typeConstPosR.end())
            return false;

        const auto typeStart = lt_it->second;

        // Search for entry in global table
        const auto gtype = globalTypes.find(gt);
        if (gtype == globalTypes.end())
            return false;

        const auto& gdata = gtype->second;

        // local wordcount and opcode
        const int     wordCount   = asWordCount(typeStart);
        const spv::Op opCode      = asOpCode(typeStart);

        // no type match if opcodes don't match, or operand count doesn't match
        if (opCode != opOpCode(gdata[0]) || wordCount != opWordCount(gdata[0]))
            return false;

        const unsigned numOperands = wordCount - 2; // all types have a result

        const auto cmpIdRange = [&](range_t range) {
            for (int x=range.first; x<std::min(range.second, wordCount); ++x)
                if (!matchType(globalTypes, asId(typeStart+x), gdata[x]))
                    return false;
            return true;
        };

        const auto cmpConst   = [&]() { return cmpIdRange(constRange(opCode)); };
        const auto cmpSubType = [&]() { return cmpIdRange(typeRange(opCode));  };

        // Compare literals in range [start,end)
        const auto cmpLiteral = [&]() {
            const auto range = literalRange(opCode);
            return std::equal(spir.begin() + typeStart + range.first,
                spir.begin() + typeStart + std::min(range.second, wordCount),
                gdata.begin() + range.first);
        };

        assert(isTypeOp(opCode) || isConstOp(opCode));

        switch (opCode) {
        case spv::OpTypeOpaque:       // TODO: disable until we compare the literal strings.
        case spv::OpTypeQueue:        return false;
        case spv::OpTypeEvent:        // fall through...
        case spv::OpTypeDeviceEvent:  // ...
        case spv::OpTypeReserveId:    return false;
            // for samplers, we don't handle the optional parameters yet
        case spv::OpTypeSampler:      return cmpLiteral() && cmpConst() && cmpSubType() && wordCount == 8;
        default:                      return cmpLiteral() && cmpConst() && cmpSubType();
        }
    }


    // Look for an equivalent type in the globalTypes map
    spv::Id spirvbin_t::findType(const spirvbin_t::globaltypes_t& globalTypes, spv::Id lt) const
    {
        // Try a recursive type match on each in turn, and return a match if we find one
        for (const auto& gt : globalTypes)
            if (matchType(globalTypes, lt, gt.first))
                return gt.first;

        return spv::NoType;
    }
#endif // NOTDEF

    // Return start position in SPV of given type.  error if not found.
    unsigned spirvbin_t::typePos(spv::Id id) const
    {
        const auto tid_it = typeConstPosR.find(id);
        if (tid_it == typeConstPosR.end())
            error("type ID not found");

        return tid_it->second;
    }

    // Hash types to canonical values.  This can return ID collisions (it's a bit
    // inevitable): it's up to the caller to handle that gracefully.
    std::uint32_t spirvbin_t::hashType(unsigned typeStart) const
    {
        const unsigned wordCount   = asWordCount(typeStart);
        const spv::Op  opCode      = asOpCode(typeStart);

        switch (opCode) {
        case spv::OpTypeVoid:         return 0;
        case spv::OpTypeBool:         return 1;
        case spv::OpTypeInt:          return 3 + (spv[typeStart+3]);
        case spv::OpTypeFloat:        return 5;
        case spv::OpTypeVector:
            return 6 + hashType(typePos(spv[typeStart+2])) * (spv[typeStart+3] - 1);
        case spv::OpTypeMatrix:
            return 30 + hashType(typePos(spv[typeStart+2])) * (spv[typeStart+3] - 1);
        case spv::OpTypeSampler:
            return 120 + hashType(typePos(spv[typeStart+2])) +
                spv[typeStart+3] +            // dimensionality
                spv[typeStart+4] * 8 * 16 +   // content
                spv[typeStart+5] * 4 * 16 +   // arrayed
                spv[typeStart+6] * 2 * 16 +   // compare
                spv[typeStart+7] * 1 * 16;    // multisampled
        case spv::OpTypeFilter:
            return 500;
        case spv::OpTypeArray:
            return 501 + hashType(typePos(spv[typeStart+2])) * spv[typeStart+3];
        case spv::OpTypeRuntimeArray:
            return 5000  + hashType(typePos(spv[typeStart+2]));
        case spv::OpTypeStruct:
            {
                std::uint32_t hash = 10000;
                for (unsigned w=2; w < wordCount; ++w)
                    hash += w * hashType(typePos(spv[typeStart+w]));
                return hash;
            }

        case spv::OpTypeOpaque:         return 6000 + spv[typeStart+2];
        case spv::OpTypePointer:        return 100000  + hashType(typePos(spv[typeStart+3]));
        case spv::OpTypeFunction:
            {
                std::uint32_t hash = 200000;
                for (unsigned w=2; w < wordCount; ++w)
                    hash += w * hashType(typePos(spv[typeStart+w]));
                return hash;
            }

        case spv::OpTypeEvent:           return 300000;
        case spv::OpTypeDeviceEvent:     return 300001;
        case spv::OpTypeReserveId:       return 300002;
        case spv::OpTypeQueue:           return 300003;
        case spv::OpTypePipe:            return 300004;

        case spv::OpConstantNullObject:  return 300005;
        case spv::OpConstantSampler:     return 300006;

        case spv::OpConstantTrue:        return 300007;
        case spv::OpConstantFalse:       return 300008;
        case spv::OpConstantNullPointer: return 300009;
        case spv::OpConstantComposite:
            {
                std::uint32_t hash = 300011 + hashType(typePos(spv[typeStart+1]));
                for (unsigned w=3; w < wordCount; ++w)
                    hash += w * hashType(typePos(spv[typeStart+w]));
                return hash;
            }
        case spv::OpConstant:
            {
                std::uint32_t hash = 400011 + hashType(typePos(spv[typeStart+1]));
                for (unsigned w=3; w < wordCount; ++w)
                    hash += w * spv[typeStart+w];
                return hash;
            }

        default:
            error("unknown type opcode");
            return 0;
        }
    }

    void spirvbin_t::mapTypeConst()
    {
        globaltypes_t globalTypeMap;

        msg(3, 2, std::string("Remapping Consts & Types: "));

        static const std::uint32_t softTypeIdLimit = 3011; // small prime.  TODO: get from options
        static const std::uint32_t firstMappedID   = 8;    // offset into ID space

        for (auto& typeStart : typeConstPos) {
            const spv::Id       resId     = asTypeConstId(typeStart);
            const std::uint32_t hashval   = hashType(typeStart);

            if (isOldIdUnmapped(resId))
                localId(resId, nextUnusedId(hashval % softTypeIdLimit + firstMappedID));
        }
    }


    // Strip a single binary by removing ranges given in stripRange
    void spirvbin_t::strip()
    {
        if (stripRange.empty()) // nothing to do
            return;

        // Sort strip ranges in order of traversal
        std::sort(stripRange.begin(), stripRange.end());

        // Allocate a new binary big enough to hold old binary
        // We'll step this iterator through the strip ranges as we go through the binary
        auto strip_it = stripRange.begin();

        int strippedPos = 0;
        for (unsigned word = 0; word < unsigned(spv.size()); ++word) {
            if (strip_it != stripRange.end() && word >= strip_it->second)
                ++strip_it;

            if (strip_it == stripRange.end() || word < strip_it->first || word >= strip_it->second)
                spv[strippedPos++] = spv[word];
        }

        spv.resize(strippedPos);
        stripRange.clear();

        buildLocalMaps();
    }

    // Strip a single binary by removing ranges given in stripRange
    void spirvbin_t::remap(std::uint32_t opts)
    {
        options = opts;

        // Set up opcode tables from SpvDoc
        spv::Parameterize();

        validate();  // validate header
        buildLocalMaps();

        msg(3, 4, std::string("ID bound: ") + std::to_string(bound()));

        strip();        // strip out data we decided to eliminate

        if (options & OPT_LOADSTORE) optLoadStore();
        if (options & OPT_FWD_LS)    forwardLoadStores();
        if (options & DCE_FUNCS)     dceFuncs();
        if (options & DCE_VARS)      dceVars();
        if (options & DCE_TYPES)     dceTypes();
        if (options & MAP_TYPES)     mapTypeConst();
        if (options & MAP_NAMES)     mapNames();
        if (options & MAP_FUNCS)     mapFnBodies();

        mapRemainder(); // map any unmapped IDs
        applyMap();     // Now remap each shader to the new IDs we've come up with
        strip();        // strip out data we decided to eliminate
    }

    // remap from a memory image
    void spirvbin_t::remap(std::vector<std::uint32_t>& in_spv, std::uint32_t opts)
    {
        spv.swap(in_spv);
        remap(opts);
        spv.swap(in_spv);
    }

} // namespace SPV

#endif // defined (use_cpp11)

