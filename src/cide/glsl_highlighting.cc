// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/glsl_highlighting.h"

#include "glslang/MachineIndependent/localintermediate.h"
#include "glslang/Public/ShaderLang.h"

#include "cide/document.h"
#include "cide/settings.h"
#include "cide/text_utils.h"

using namespace glslang;

class GLSLTraverser : public glslang::TIntermTraverser {
 public:
  inline GLSLTraverser(Document* document, const QString& documentContent, const std::vector<unsigned>& lineOffsets)
      : document(document),
        documentContent(documentContent),
        lineOffsets(lineOffsets),
        perVariableColoring(Settings::Instance().GetUsePerVariableColoring()) {}
  
  // The functions below must return true to have the external traversal
  // continue on to the childred. If they would traverse the children themselves,
  // they could return false instead.
  
  // See glslang/MachineIndependent/intermOut.cpp for an example that prints the tree while traversing it.
  
  virtual bool visitBinary(TVisit, TIntermBinary* /*node*/) override {
    // TODO
    
    return true;
  }
  
  virtual bool visitUnary(TVisit, TIntermUnary* /*node*/) override {
    // TODO
    
    return true;
  }
  
  virtual bool visitAggregate(TVisit, TIntermAggregate* node) override {
    if (node->getLoc().line == 0) { return true; }  // seemingly no valid location information
    
    const auto& functionDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::FunctionDefinition);
    const auto& functionUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::FunctionUse);
    
    DocumentRange range;
    switch (node->getOp()) {
    case EOpSequence:
    case EOpParameters:
    case EOpLessThan:
    case EOpGreaterThan:
    case EOpLessThanEqual:
    case EOpGreaterThanEqual:
    case EOpVectorEqual:
    case EOpVectorNotEqual:
    case EOpComma:
      break;
    
    case EOpFunction: {
      variableCounterPerFunction = 0;

      // For "main()", the loc is between the braces, and node->getName() is "main(".
      // For a function with arguments, the name is something like "AtomicAddStore(vu4;vi2;i1;i1;".
      // In general, the loc is right before the end brace, for example at the '|' in:
      // int helper_function(int a, float b|)
      // Thus, we heuristically search back from the loc to find the function name.
      QString nodeName = QString::fromUtf8(node->getName().c_str());
      int openBraceInNodeName = nodeName.indexOf('(');
      if (openBraceInNodeName >= 0) {
        nodeName = nodeName.mid(0, openBraceInNodeName);

        range.start = documentContent.lastIndexOf(nodeName, LocToOffset(node->getLoc()), Qt::CaseSensitive);
        if (range.start >= 0) {
          range.end = range.start + nodeName.size();
          document->AddHighlightRange(range, false, functionDefinitionStyle);
        }
      }
      break; }
    
    case EOpFunctionCall:
    case EOpConstructFloat:
    case EOpConstructDouble:
    case EOpConstructVec2:
    case EOpConstructVec3:
    case EOpConstructVec4:
    case EOpConstructDVec2:
    case EOpConstructDVec3:
    case EOpConstructDVec4:
    case EOpConstructBool:
    case EOpConstructBVec2:
    case EOpConstructBVec3:
    case EOpConstructBVec4:
    case EOpConstructInt8:
    case EOpConstructI8Vec2:
    case EOpConstructI8Vec3:
    case EOpConstructI8Vec4:
    case EOpConstructInt:
    case EOpConstructIVec2:
    case EOpConstructIVec3:
    case EOpConstructIVec4:
    case EOpConstructUint8:
    case EOpConstructU8Vec2:
    case EOpConstructU8Vec3:
    case EOpConstructU8Vec4:
    case EOpConstructUint:
    case EOpConstructUVec2:
    case EOpConstructUVec3:
    case EOpConstructUVec4:
    case EOpConstructInt64:
    case EOpConstructI64Vec2:
    case EOpConstructI64Vec3:
    case EOpConstructI64Vec4:
    case EOpConstructUint64:
    case EOpConstructU64Vec2:
    case EOpConstructU64Vec3:
    case EOpConstructU64Vec4:
    case EOpConstructInt16:
    case EOpConstructI16Vec2:
    case EOpConstructI16Vec3:
    case EOpConstructI16Vec4:
    case EOpConstructUint16:
    case EOpConstructU16Vec2:
    case EOpConstructU16Vec3:
    case EOpConstructU16Vec4:
    case EOpConstructMat2x2:
    case EOpConstructMat2x3:
    case EOpConstructMat2x4:
    case EOpConstructMat3x2:
    case EOpConstructMat3x3:
    case EOpConstructMat3x4:
    case EOpConstructMat4x2:
    case EOpConstructMat4x3:
    case EOpConstructMat4x4:
    case EOpConstructDMat2x2:
    case EOpConstructDMat2x3:
    case EOpConstructDMat2x4:
    case EOpConstructDMat3x2:
    case EOpConstructDMat3x3:
    case EOpConstructDMat3x4:
    case EOpConstructDMat4x2:
    case EOpConstructDMat4x3:
    case EOpConstructDMat4x4:
    case EOpConstructIMat2x2:
    case EOpConstructIMat2x3:
    case EOpConstructIMat2x4:
    case EOpConstructIMat3x2:
    case EOpConstructIMat3x3:
    case EOpConstructIMat3x4:
    case EOpConstructIMat4x2:
    case EOpConstructIMat4x3:
    case EOpConstructIMat4x4:
    case EOpConstructUMat2x2:
    case EOpConstructUMat2x3:
    case EOpConstructUMat2x4:
    case EOpConstructUMat3x2:
    case EOpConstructUMat3x3:
    case EOpConstructUMat3x4:
    case EOpConstructUMat4x2:
    case EOpConstructUMat4x3:
    case EOpConstructUMat4x4:
    case EOpConstructBMat2x2:
    case EOpConstructBMat2x3:
    case EOpConstructBMat2x4:
    case EOpConstructBMat3x2:
    case EOpConstructBMat3x3:
    case EOpConstructBMat3x4:
    case EOpConstructBMat4x2:
    case EOpConstructBMat4x3:
    case EOpConstructBMat4x4:
    case EOpConstructFloat16:
    case EOpConstructF16Vec2:
    case EOpConstructF16Vec3:
    case EOpConstructF16Vec4:
    case EOpConstructF16Mat2x2:
    case EOpConstructF16Mat2x3:
    case EOpConstructF16Mat2x4:
    case EOpConstructF16Mat3x2:
    case EOpConstructF16Mat3x3:
    case EOpConstructF16Mat3x4:
    case EOpConstructF16Mat4x2:
    case EOpConstructF16Mat4x3:
    case EOpConstructF16Mat4x4:
    case EOpConstructStruct:
    case EOpConstructTextureSampler:
    case EOpConstructReference:
    case EOpConstructCooperativeMatrix:
    case EOpMod:
    case EOpModf:
    case EOpPow:
    case EOpAtan:
    case EOpMin:
    case EOpMax:
    case EOpClamp:
    case EOpMix:
    case EOpStep:
    case EOpSmoothStep:
    case EOpDistance:
    case EOpDot:
    case EOpCross:
    case EOpFaceForward:
    case EOpReflect:
    case EOpRefract:
    case EOpMul:
    case EOpOuterProduct:
    case EOpEmitVertex:
    case EOpEndPrimitive:
    case EOpBarrier:
    case EOpMemoryBarrier:
    case EOpMemoryBarrierAtomicCounter:
    case EOpMemoryBarrierBuffer:
    case EOpMemoryBarrierImage:
    case EOpMemoryBarrierShared:
    case EOpGroupMemoryBarrier:
    case EOpReadInvocation:
    case EOpSwizzleInvocations:
    case EOpSwizzleInvocationsMasked:
    case EOpWriteInvocation:
    case EOpMin3:
    case EOpMax3:
    case EOpMid3:
    case EOpTime:
    case EOpAtomicAdd:
    case EOpAtomicMin:
    case EOpAtomicMax:
    case EOpAtomicAnd:
    case EOpAtomicOr:
    case EOpAtomicXor:
    case EOpAtomicExchange:
    case EOpAtomicCompSwap:
    case EOpAtomicLoad:
    case EOpAtomicStore:
    case EOpAtomicCounterAdd:
    case EOpAtomicCounterSubtract:
    case EOpAtomicCounterMin:
    case EOpAtomicCounterMax:
    case EOpAtomicCounterAnd:
    case EOpAtomicCounterOr:
    case EOpAtomicCounterXor:
    case EOpAtomicCounterExchange:
    case EOpAtomicCounterCompSwap:
    case EOpImageQuerySize:
    case EOpImageQuerySamples:
    case EOpImageLoad:
    case EOpImageStore:
    case EOpImageAtomicAdd:
    case EOpImageAtomicMin:
    case EOpImageAtomicMax:
    case EOpImageAtomicAnd:
    case EOpImageAtomicOr:
    case EOpImageAtomicXor:
    case EOpImageAtomicExchange:
    case EOpImageAtomicCompSwap:
    case EOpImageAtomicLoad:
    case EOpImageAtomicStore:
    case EOpImageLoadLod:
    case EOpImageStoreLod:
    case EOpTextureQuerySize:
    case EOpTextureQueryLod:
    case EOpTextureQueryLevels:
    case EOpTextureQuerySamples:
    case EOpTexture:
    case EOpTextureProj:
    case EOpTextureLod:
    case EOpTextureOffset:
    case EOpTextureFetch:
    case EOpTextureFetchOffset:
    case EOpTextureProjOffset:
    case EOpTextureLodOffset:
    case EOpTextureProjLod:
    case EOpTextureProjLodOffset:
    case EOpTextureGrad:
    case EOpTextureGradOffset:
    case EOpTextureProjGrad:
    case EOpTextureProjGradOffset:
    case EOpTextureGather:
    case EOpTextureGatherOffset:
    case EOpTextureGatherOffsets:
    case EOpTextureClamp:
    case EOpTextureOffsetClamp:
    case EOpTextureGradClamp:
    case EOpTextureGradOffsetClamp:
    case EOpTextureGatherLod:
    case EOpTextureGatherLodOffset:
    case EOpTextureGatherLodOffsets:
    case EOpSparseTexture:
    case EOpSparseTextureOffset:
    case EOpSparseTextureLod:
    case EOpSparseTextureLodOffset:
    case EOpSparseTextureFetch:
    case EOpSparseTextureFetchOffset:
    case EOpSparseTextureGrad:
    case EOpSparseTextureGradOffset:
    case EOpSparseTextureGather:
    case EOpSparseTextureGatherOffset:
    case EOpSparseTextureGatherOffsets:
    case EOpSparseImageLoad:
    case EOpSparseTextureClamp:
    case EOpSparseTextureOffsetClamp:
    case EOpSparseTextureGradClamp:
    case EOpSparseTextureGradOffsetClamp:
    case EOpSparseTextureGatherLod:
    case EOpSparseTextureGatherLodOffset:
    case EOpSparseTextureGatherLodOffsets:
    case EOpSparseImageLoadLod:
    case EOpImageSampleFootprintNV:
    case EOpImageSampleFootprintClampNV:
    case EOpImageSampleFootprintLodNV:
    case EOpImageSampleFootprintGradNV:
    case EOpImageSampleFootprintGradClampNV:
    case EOpAddCarry:
    case EOpSubBorrow:
    case EOpUMulExtended:
    case EOpIMulExtended:
    case EOpBitfieldExtract:
    case EOpBitfieldInsert:
    case EOpFma:
    case EOpFrexp:
    case EOpLdexp:
    case EOpInterpolateAtSample:
    case EOpInterpolateAtOffset:
    case EOpInterpolateAtVertex:
    case EOpSinCos:
    case EOpGenMul:
    case EOpAllMemoryBarrierWithGroupSync:
    case EOpDeviceMemoryBarrier:
    case EOpDeviceMemoryBarrierWithGroupSync:
    case EOpWorkgroupMemoryBarrier:
    case EOpWorkgroupMemoryBarrierWithGroupSync:
    case EOpSubgroupBarrier:
    case EOpSubgroupMemoryBarrier:
    case EOpSubgroupMemoryBarrierBuffer:
    case EOpSubgroupMemoryBarrierImage:
    case EOpSubgroupMemoryBarrierShared:
    case EOpSubgroupElect:
    case EOpSubgroupAll:
    case EOpSubgroupAny:
    case EOpSubgroupAllEqual:
    case EOpSubgroupBroadcast:
    case EOpSubgroupBroadcastFirst:
    case EOpSubgroupBallot:
    case EOpSubgroupInverseBallot:
    case EOpSubgroupBallotBitExtract:
    case EOpSubgroupBallotBitCount:
    case EOpSubgroupBallotInclusiveBitCount:
    case EOpSubgroupBallotExclusiveBitCount:
    case EOpSubgroupBallotFindLSB:
    case EOpSubgroupBallotFindMSB:
    case EOpSubgroupShuffle:
    case EOpSubgroupShuffleXor:
    case EOpSubgroupShuffleUp:
    case EOpSubgroupShuffleDown:
    case EOpSubgroupAdd:
    case EOpSubgroupMul:
    case EOpSubgroupMin:
    case EOpSubgroupMax:
    case EOpSubgroupAnd:
    case EOpSubgroupOr:
    case EOpSubgroupXor:
    case EOpSubgroupInclusiveAdd:
    case EOpSubgroupInclusiveMul:
    case EOpSubgroupInclusiveMin:
    case EOpSubgroupInclusiveMax:
    case EOpSubgroupInclusiveAnd:
    case EOpSubgroupInclusiveOr:
    case EOpSubgroupInclusiveXor:
    case EOpSubgroupExclusiveAdd:
    case EOpSubgroupExclusiveMul:
    case EOpSubgroupExclusiveMin:
    case EOpSubgroupExclusiveMax:
    case EOpSubgroupExclusiveAnd:
    case EOpSubgroupExclusiveOr:
    case EOpSubgroupExclusiveXor:
    case EOpSubgroupClusteredAdd:
    case EOpSubgroupClusteredMul:
    case EOpSubgroupClusteredMin:
    case EOpSubgroupClusteredMax:
    case EOpSubgroupClusteredAnd:
    case EOpSubgroupClusteredOr:
    case EOpSubgroupClusteredXor:
    case EOpSubgroupQuadBroadcast:
    case EOpSubgroupQuadSwapHorizontal:
    case EOpSubgroupQuadSwapVertical:
    case EOpSubgroupQuadSwapDiagonal:
    case EOpSubgroupPartition:
    case EOpSubgroupPartitionedAdd:
    case EOpSubgroupPartitionedMul:
    case EOpSubgroupPartitionedMin:
    case EOpSubgroupPartitionedMax:
    case EOpSubgroupPartitionedAnd:
    case EOpSubgroupPartitionedOr:
    case EOpSubgroupPartitionedXor:
    case EOpSubgroupPartitionedInclusiveAdd:
    case EOpSubgroupPartitionedInclusiveMul:
    case EOpSubgroupPartitionedInclusiveMin:
    case EOpSubgroupPartitionedInclusiveMax:
    case EOpSubgroupPartitionedInclusiveAnd:
    case EOpSubgroupPartitionedInclusiveOr:
    case EOpSubgroupPartitionedInclusiveXor:
    case EOpSubgroupPartitionedExclusiveAdd:
    case EOpSubgroupPartitionedExclusiveMul:
    case EOpSubgroupPartitionedExclusiveMin:
    case EOpSubgroupPartitionedExclusiveMax:
    case EOpSubgroupPartitionedExclusiveAnd:
    case EOpSubgroupPartitionedExclusiveOr:
    case EOpSubgroupPartitionedExclusiveXor:
    case EOpSubpassLoad:
    case EOpSubpassLoadMS:
    // case EOpTrace:
    case EOpReportIntersection:
    case EOpIgnoreIntersectionNV:
    case EOpIgnoreIntersectionKHR:
    case EOpTerminateRayNV:
    case EOpTerminateRayKHR:
    case EOpExecuteCallableNV:
    case EOpExecuteCallableKHR:
    case EOpWritePackedPrimitiveIndices4x8NV:
    case EOpRayQueryInitialize:
    case EOpRayQueryTerminate:
    case EOpRayQueryGenerateIntersection:
    case EOpRayQueryConfirmIntersection:
    case EOpRayQueryProceed:
    case EOpRayQueryGetIntersectionType:
    case EOpRayQueryGetRayTMin:
    case EOpRayQueryGetRayFlags:
    case EOpRayQueryGetIntersectionT:
    case EOpRayQueryGetIntersectionInstanceCustomIndex:
    case EOpRayQueryGetIntersectionInstanceId:
    case EOpRayQueryGetIntersectionInstanceShaderBindingTableRecordOffset:
    case EOpRayQueryGetIntersectionGeometryIndex:
    case EOpRayQueryGetIntersectionPrimitiveIndex:
    case EOpRayQueryGetIntersectionBarycentrics:
    case EOpRayQueryGetIntersectionFrontFace:
    case EOpRayQueryGetIntersectionCandidateAABBOpaque:
    case EOpRayQueryGetIntersectionObjectRayDirection:
    case EOpRayQueryGetIntersectionObjectRayOrigin:
    case EOpRayQueryGetWorldRayDirection:
    case EOpRayQueryGetWorldRayOrigin:
    case EOpRayQueryGetIntersectionObjectToWorld:
    case EOpRayQueryGetIntersectionWorldToObject:
    case EOpCooperativeMatrixLoad:
    case EOpCooperativeMatrixStore:
    case EOpCooperativeMatrixMulAdd:
    case EOpIsHelperInvocation:
    case EOpDebugPrintf:
      document->AddHighlightRange(FindFunctionCallRangeHeuristic(node), false, functionUseStyle); break;
    
    default:
      qDebug() << "Unhandled GLSL aggregate case: " << node->getOp();
    }
    
    // qDebug() << "Aggregate: '" << node->getName().c_str() << "' (" << node->getCompleteString().c_str() << ") at: " << node->getLoc().line << ", " << node->getLoc().column;
    // qDebug() << "Aggregate's sequence size: " << node->getSequence().size();
    // if (!node->getSequence().empty()) {
    //   qDebug() << "  First loc: " << node->getSequence()[0]->getLoc().line << ", " << node->getSequence()[0]->getLoc().column;
    // }
    return true;
  }
  
  virtual bool visitSelection(TVisit, TIntermSelection* /*node*/) override {
    // TODO
    
    // qDebug() << "Selection at: " << node->getLoc().line << ", " << node->getLoc().column;
    return true;
  }
  
  virtual void visitConstantUnion(TIntermConstantUnion* /*node*/) override {
    // TODO
    
    // qDebug() << "ConstantUnion at: " << node->getLoc().line << ", " << node->getLoc().column;
  }
  
  virtual void visitSymbol(TIntermSymbol* node) override {
    if (node->getLoc().line == 0) { return; }  // seemingly no valid location information
    
    // TODO: Never generate a definition for built-in variables such as for example gl_GlobalInvocationID.
    // TODO: Also, descriptors should not be treated as a definition the first time they are used in the shader code.
    QColor overrideColor;
    bool newColor;
    auto it = perVariableColorMap.find(node->getId());
    newColor = it == perVariableColorMap.end();
    if (!newColor) {
      overrideColor = it->second;
    } else {
      overrideColor = Settings::Instance().GetLocalVariableColor(variableCounterPerFunction % Settings::Instance().GetLocalVariableColorPoolSize());
      perVariableColorMap[node->getId()] = overrideColor;
      ++ variableCounterPerFunction;
    }
    
    bool isDefinition = newColor;
    if (node->getQualifier().hasLayout() ||
        node->getQualifier().hasLocation() ||
        node->getQualifier().hasBinding() ||
        node->getQualifier().hasSpecConstantId() ||
        node->getQualifier().isUniformOrBuffer() ||
        node->getQualifier().isFrontEndConstant() ||
        node->getQualifier().isPipeInput() ||
        node->getQualifier().isPipeOutput() ||
        node->getQualifier().isSpecConstant() ||
        node->getQualifier().isPushConstant()) {
      isDefinition = false;
    }
    
    const auto& variableDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::VariableDefinition);
    const auto& variableUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::VariableUse);
    const Settings::ConfigurableTextStyle* style = isDefinition ? &variableDefinitionStyle : &variableUseStyle;
    
    if (perVariableColoring) {
      // We usually override the text color, but do override the background color instead if the style does not affect the text color.
      document->AddHighlightRange(GetNameRange(node), false, overrideColor, style->bold, style->affectsText, style->affectsBackground, style->affectsText ? style->backgroundColor : overrideColor);
    } else {
      document->AddHighlightRange(GetNameRange(node), false, *style);
    }
    
    // qDebug() << "Symbol: '" << node->getName().c_str() << "' (" << node->getCompleteString().c_str() << ") at: " << node->getLoc().line << ", " << node->getLoc().column;
  }
  
  virtual bool visitLoop(TVisit, TIntermLoop* /*node*/) override {
    // TODO
    
    // qDebug() << "Loop at: " << node->getLoc().line << ", " << node->getLoc().column;
    return true;
  }
  
  virtual bool visitBranch(TVisit, TIntermBranch* /*node*/) override {
    // TODO
    
    // qDebug() << "Branch at: " << node->getLoc().line << ", " << node->getLoc().column;
    return true;
  }
  
  virtual bool visitSwitch(TVisit, TIntermSwitch* /*node*/) override {
    // TODO
    
    // qDebug() << "Switch at: " << node->getLoc().line << ", " << node->getLoc().column;
    return true;
  }
  
 protected:
  DocumentRange GetNameRange(const TIntermAggregate* node) {
    int startOffset = LocToOffset(node->getLoc());
    return DocumentRange(startOffset, startOffset + node->getName().size());
  }
  
  DocumentRange GetNameRange(const TIntermSymbol* node) {
    // Some symbols seem to be named like "anon@0".
    // This happened for an access to an un-sized array.
    if (node->getName().find_first_of('@') != std::string::npos) {
      return FindWordRangeHeuristic(node->getLoc());
    }
    
    int startOffset = LocToOffset(node->getLoc());
    return DocumentRange(startOffset, startOffset + node->getName().size());
  }
  
  DocumentRange FindWordRangeHeuristic(const TSourceLoc& startLoc) {
    int documentSize = documentContent.size();
    int startOffset = LocToOffset(startLoc);
    
    int offset = startOffset;
    while (offset < documentSize) {
      QChar ch = documentContent[offset];
      if (!IsIdentifierChar(ch)) {
        break;
      }
      ++ offset;
    }
    
    return DocumentRange(startOffset, offset);
  }
  
  DocumentRange FindFunctionCallRangeHeuristic(TIntermAggregate* node) {
    // It seems that the loc of function call aggregates is on the closing brace of their parameter list.
    
    // Search for the opening brace. If there is a sequence, we assume that this represents the
    // parameter list. So we start searching before the loc of the first parameter.
    int startOffset;
    if (node->getSequence().empty()) {
      startOffset = LocToOffset(node->getLoc()) - 1;
    } else {
      startOffset = LocToOffset(node->getSequence()[0]->getLoc()) - 1;
    }
    
    // Search for the '(' at or preceding the current value of startOffset.
    bool found = false;
    while (startOffset > 0) {
      QChar ch = documentContent[startOffset];
      if (ch == '(') {
        found = true;
        break;
      } else if (!IsWhitespace(ch)) {
        // Something went wrong.
        return DocumentRange::Invalid();
      }
      -- startOffset;
    }
    if (!found) {
      // Something went wrong.
      return DocumentRange::Invalid();
    }
    
    -- startOffset;
    
    // Skip over potential spaces between the opening brace and the function call name
    while (startOffset > 0) {
      QChar ch = documentContent[startOffset];
      if (!IsWhitespace(ch)) {
        break;
      }
      -- startOffset;
    }
    
    int endOffset = startOffset + 1;
    
    // Skip over all identifier characters (the function call name)
    while (startOffset > 0) {
      QChar ch = documentContent[startOffset];
      if (!IsIdentifierChar(ch)) {
        break;
      }
      -- startOffset;
    }
    ++ startOffset;
    
    return DocumentRange(startOffset, endOffset);
  }
  
  int LocToOffset(const TSourceLoc& loc) {
    return std::min<int>(documentContent.size(), lineOffsets[std::max(0, std::min<int>(lineOffsets.size() - 1, loc.line - 1))] + loc.column - 1);
  }
  
  GLSLTraverser(GLSLTraverser&) = delete;
  GLSLTraverser& operator=(GLSLTraverser&) = delete;
  
  /// Maps variable IDs to their assigned colors.
  std::unordered_map<int, QColor> perVariableColorMap;
  int variableCounterPerFunction = 0;
  
  Document* document;
  const QString& documentContent;
  const std::vector<unsigned>& lineOffsets;
  
  bool perVariableColoring;
};

void AddGLSLHighlighting(Document* document, const QString& documentContent, glslang::TIntermediate* ast, const std::vector<unsigned>& lineOffsets) {
  if (!ast->getTreeRoot()) {
    return;
  }
  
  // EShLanguage shaderStage = ast->getStage();
  
  // TODO: Is this the correct wat to treat glslang's memory pool handling?
  TPoolAllocator& previousAllocator = GetThreadPoolAllocator();
  TPoolAllocator* builtInPoolAllocator = new TPoolAllocator;
  SetThreadPoolAllocator(builtInPoolAllocator);
  
  GLSLTraverser traverser(document, documentContent, lineOffsets);
  ast->getTreeRoot()->traverse(&traverser);
  
  delete builtInPoolAllocator;
  SetThreadPoolAllocator(&previousAllocator);
}
