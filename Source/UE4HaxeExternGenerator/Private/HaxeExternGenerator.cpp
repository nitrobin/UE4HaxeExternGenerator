#include "IHaxeExternGenerator.h"
#include <Features/IModularFeatures.h>
#include <CoreUObject.h>
#include "HaxeGenerator.h"
#include "HaxeTypes.h"

DEFINE_LOG_CATEGORY(LogHaxeExtern);

static const FName NAME_ToolTip(TEXT("ToolTip"));

class FHaxeExternGenerator : public IHaxeExternGenerator {
protected:
  FString m_pluginPath;
  FHaxeTypes m_types;
  static FString currentModule;
public:

  virtual void StartupModule() override {
    IModularFeatures::Get().RegisterModularFeature(TEXT("ScriptGenerator"), this);
  }

  virtual void ShutdownModule() override {
    IModularFeatures::Get().UnregisterModularFeature(TEXT("ScriptGenerator"), this);
  }

  /** Name of module that is going to be compiling generated script glue */
  virtual FString GetGeneratedCodeModuleName() const override {
    return TEXT("HaxeInit");
  }

  /** Returns true if this plugin supports exporting scripts for the specified target. This should handle game as well as editor target names */
  virtual bool SupportsTarget(const FString& TargetName) const override { 
    UE_LOG(LogHaxeExtern,Log,TEXT("SUPPORTS %s"), *TargetName);
    TCHAR env[2];
    FPlatformMisc::GetEnvironmentVariable(TEXT("GENERATE_EXTERNS"), env, 2);
    return *env;
  }
  /** Returns true if this plugin supports exporting scripts for the specified module */
  virtual bool ShouldExportClassesForModule(const FString& ModuleName, EBuildModuleType::Type ModuleType, const FString& ModuleGeneratedIncludeDirectory) const override {
    UE_LOG(LogHaxeExtern,Log,TEXT("SHOULD EXPORT %s (inc %s)"), *ModuleName, *ModuleGeneratedIncludeDirectory);
    currentModule = ModuleName;
    return true;
  }

  /** Initializes this plugin with build information */
  virtual void Initialize(const FString& RootLocalPath, const FString& RootBuildPath, const FString& OutputDirectory, const FString& IncludeBase) override {
    UE_LOG(LogHaxeExtern,Log,TEXT("INITIALIZE %s %s %s %s"), *RootLocalPath, *RootBuildPath, *OutputDirectory, *IncludeBase);
    this->m_pluginPath = IncludeBase + TEXT("/../../");
  }

  /** Exports a single class. May be called multiple times for the same class (as UHT processes the entire hierarchy inside modules. */
  virtual void ExportClass(class UClass* Class, const FString& SourceHeaderFilename, const FString& GeneratedHeaderFilename, bool bHasChanged) override {
    UE_LOG(LogHaxeExtern,Log,TEXT("EXPORT CLASS %s %s %s %s"), *(Class->GetDesc()), *SourceHeaderFilename, *GeneratedHeaderFilename, bHasChanged ? TEXT("CHANGED") : TEXT("NOT CHANGED"));
    FString comment = Class->GetMetaData(NAME_ToolTip);
    m_types.touchClass(Class, SourceHeaderFilename, currentModule);
  }

  void saveFile(const FHaxeTypeRef& inHaxeType, const FString& contents) {
    auto& fileMan = IFileManager::Get();
    auto outPath = this->m_pluginPath / TEXT("Haxe/Externs") / FString::Join(inHaxeType.pack, TEXT("/"));
    if (!fileMan.DirectoryExists(*outPath)) {
      fileMan.MakeDirectory(*outPath, true);
    }

    auto file = outPath / inHaxeType.name + TEXT(".hx");
    if (!FFileHelper::SaveStringToFile(contents, *file, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) {
      UE_LOG(LogHaxeExtern, Fatal, TEXT("Cannot write file at path %s"), *file);
    }
  }

  /** Called once all classes have been exported */
  virtual void FinishExport() override {
    UE_LOG(LogHaxeExtern,Log,TEXT("FINISH_EXPORT 1"));
    // now start generating
    for (auto& cls : m_types.getAllClasses()) {
      UE_LOG(LogHaxeExtern,Log,TEXT("got %s"), *cls->uclass->GetName())
      auto gen = FHaxeGenerator(this->m_types, this->m_pluginPath);
      gen.generateClass(cls);
      saveFile(cls->haxeType, gen.toString());
    }

    UE_LOG(LogHaxeExtern,Log,TEXT("STARTING USTRUCTS"));
    for (auto& s : m_types.getAllStructs()) {
      UE_LOG(LogHaxeExtern,Log,TEXT("BEGIN"));
      UE_LOG(LogHaxeExtern,Log,TEXT("got %s"), *s->ustruct->GetName())
      auto gen = FHaxeGenerator(this->m_types, this->m_pluginPath);
      gen.generateStruct(s);
      saveFile(s->haxeType, gen.toString());
    }

    UE_LOG(LogHaxeExtern,Log,TEXT("STARTING UENUMs"));
    for (auto& uenum : m_types.getAllEnums()) {
      UE_LOG(LogHaxeExtern,Log,TEXT("BEGIN"));
      UE_LOG(LogHaxeExtern,Log,TEXT("got %s"), *uenum->uenum->GetName())
      auto gen = FHaxeGenerator(this->m_types, this->m_pluginPath);
      gen.generateEnum(uenum);
      saveFile(uenum->haxeType, gen.toString());
    }
  }

  /** Name of the generator plugin, mostly for debuggind purposes */
  virtual FString GetGeneratorName() const override {
    return TEXT("Haxe Extern Generator Plugin");
  }

};

static const FString prelude = TEXT(
  "This file was autogenerated by UE4HaxeExternGenerator using UHT definitions. It only includes UPROPERTYs and UFUNCTIONs. Do not modify it!\n"
  "In order to add more definitions, create or edit a type with the same name/package, but with a `_Extra` suffix");

IMPLEMENT_MODULE(FHaxeExternGenerator, UE4HaxeExternGenerator)

FString FHaxeExternGenerator::currentModule = FString();

FString FHaxeGenerator::getHeaderPath(UPackage *inPack, const FString& inPath) {
  static const TCHAR *Engineh = TEXT("Engine.h");
  if (inPath.IsEmpty()) {
    // this is a particularity of UHT - it sometimes adds no header path to some of the core UObjects
    return FString("CoreUObject.h");
  } else if (inPath == Engineh) {
    return Engineh;
  }
  int32 index = inPath.Find(TEXT("Public"), ESearchCase::IgnoreCase, ESearchDir::FromEnd, inPath.Len());
  if (index < 0) {
    index = inPath.Find(TEXT("Classes"), ESearchCase::IgnoreCase, ESearchDir::FromEnd, inPath.Len());
    if (index >= 0)
      index += sizeof("Classes");
  } else {
    index += sizeof("Public");
  }
  if (index < 0) {
    auto pack = inPack->GetName().RightChop( sizeof("/Script") );
    auto lastSlash = inPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, inPath.Len());
    auto lastBackslash = inPath.Find(TEXT("\\"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, inPath.Len());
    int startPos = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
    index = inPath.Find(pack, ESearchCase::IgnoreCase, ESearchDir::FromEnd, startPos);
    if (index >= 0)
      index += pack.Len() + 1;
  }
  if (index < 0) {
    index = inPath.Find(TEXT("Private"), ESearchCase::IgnoreCase, ESearchDir::FromEnd, inPath.Len());
  }
  if (index >= 0) {
    int len = inPath.Len();
    while (len > ++index && (inPath[index] == TCHAR('/') || inPath[index] == TCHAR('\\'))) {
      //advance index
    }
    LOG("%s: %s", *inPath, *inPath.RightChop(index - 1));
    return inPath.RightChop(index - 1);
  }

  UE_LOG(LogHaxeExtern, Fatal, TEXT("Cannot determine header path of %s on package %s"), *inPath, *inPack->GetName());
  return FString();
}

void FHaxeGenerator::generateFields(UStruct *inStruct) {
  UClass *uclass = nullptr;
  if (inStruct->IsA<UClass>()) {
    uclass = Cast<UClass>(inStruct);
  }
  auto wasEditorOnly = false;
  TArray<UField *> fields;
  for (TFieldIterator<UField> invFields(inStruct, EFieldIteratorFlags::ExcludeSuper); invFields; ++invFields) {
    fields.Push(*invFields);
  }
  while (fields.Num() > 0) {
    // reverse field iterator so fields are declared in the same order as C++ code
    auto field = fields.Pop(false);
    if (field->IsA<UProperty>()) {
      auto prop = Cast<UProperty>(field);
      if (prop->HasAnyPropertyFlags(CPF_Protected) && prop->IsA<UBoolProperty>()) {
        continue; // we cannot generate code for protected bit-fields
      }
      FString type;
      if ((prop->HasAnyFlags(RF_Public) || prop->HasAnyPropertyFlags(CPF_Protected)) && upropType(prop, type)) {
        auto isEditorOnly = prop->HasAnyPropertyFlags(CPF_EditorOnly);
        if (isEditorOnly != wasEditorOnly) {
          if (isEditorOnly) {
            m_buf << TEXT("#if WITH_EDITORONLY_DATA") << Newline();
          } else {
            m_buf << TEXT("#end // WITH_EDITORONLY_DATA") << Newline();
          }
          wasEditorOnly = isEditorOnly;
        }
        auto& propComment = prop->GetMetaData(NAME_ToolTip);
        if (!propComment.IsEmpty()) {
          m_buf << Comment(propComment);
        }
        auto readOnly = prop->HasAnyPropertyFlags(CPF_ConstParm);
        m_buf 
          << (prop->HasAnyPropertyFlags(CPF_Protected) ? TEXT("private var ") : TEXT("public var ")) 
          << (readOnly ? TEXT("(default,never)") : TEXT(""))
          << prop->GetNameCPP();
        // TODO see if the property is read-only; this might not be supported by UHT atm?
        // if (prop->HasAnyPropertyFlags( CPF_Con
        m_buf << TEXT(" : ") << type << TEXT(";") << Newline();
      }
    } else if (field->IsA<UFunction>()) {
      auto func = Cast<UFunction>(field);
      if ((uclass != nullptr && func->GetOwnerClass() != uclass) || func->GetOwnerStruct() != inStruct) {
        // we don't need to generate overridden functions' glue code
        continue;
      } else if (func->HasAnyFunctionFlags(FUNC_Private)) {
        // we can't access private functions
        continue;
      }
      // we need to create a local buffer because we will only know if we should
      // generate this function in the end of its processing
      FHelperBuf curBuf;

      if (func->HasAnyFunctionFlags(FUNC_Const)) {
        curBuf << TEXT("@:thisConst ");
      }

      LOG("Generating %s (flags %x)", *func->GetName(), func->FunctionFlags);
      if (func->HasAnyFunctionFlags(FUNC_Static)) {
        curBuf << TEXT("static ");
      } else if (func->HasAnyFunctionFlags(FUNC_Final)) {
        curBuf << TEXT("@:final ");
      }
      curBuf << (func->HasAnyFunctionFlags(FUNC_Public) ? TEXT("public function ") : TEXT("private function ")) << func->GetName() << TEXT("(");
      auto first = true;
      auto shouldExport = true;
      bool hasReturnValue = false;
      for (TFieldIterator<UProperty> params(func); params; ++params) {
        check(!hasReturnValue);
        auto param = *params;
        FString type;
        if (upropType(param, type)) {
          if (param->HasAnyPropertyFlags(CPF_ReturnParm)) {
            hasReturnValue = true;
            curBuf << TEXT(") : ") << type;
          } else {
            if (first) first = false; else curBuf << TEXT(", ");
            curBuf << param->GetNameCPP() << TEXT(" : ") << type;
          }
        } else {
          shouldExport = false;
          break;
        }
      }
      if (!hasReturnValue) {
        curBuf << TEXT(") : Void;");
      } else {
        curBuf << TEXT(";");
      }
      if (shouldExport) {
        // seems like UHT doesn't support editor-only ufunctions
        if (wasEditorOnly) {
          wasEditorOnly = false;
          m_buf << TEXT("#end // WITH_EDITORONLY_DATA") << Newline();
        }
        auto& fnComment = func->GetMetaData(NAME_ToolTip);
        if (!fnComment.IsEmpty()) {
          m_buf << Comment(fnComment);
        }
        m_buf << curBuf.toString() << Newline();
      }
    } else {
      LOG("Field %s is not a UFUNCTION or UPROERTY", *field->GetName());
    }
  }
  if (wasEditorOnly) {
    wasEditorOnly = false;
    m_buf << TEXT("#end // WITH_EDITORONLY_DATA") << Newline();
  }
}

bool FHaxeGenerator::generateClass(const ClassDescriptor *inClass) {
  auto hxType = inClass->haxeType;
  m_buf << Comment(prelude);

  if (hxType.pack.Num() > 0) {
    m_buf << TEXT("package ") << FString::Join(hxType.pack, TEXT(".")) << ";" << Newline() << Newline();
  }
  
  auto isInterface = hxType.kind == ETypeKind::KUInterface;
  auto uclass = inClass->uclass;
  bool isNoExport = (uclass->ClassFlags & CLASS_NoExport);
  // comment
  auto comment = uclass->GetMetaData(NAME_ToolTip);
  if (isNoExport) {
    comment = TEXT("WARNING: This types is defined as NoExport by UHT. It will be empty because of it\n\n") + comment;
  }

  if (!comment.IsEmpty()) {
    m_buf << Comment(comment);
  }
  // @:umodule
  if (!hxType.module.IsEmpty()) {
    m_buf << TEXT("@:umodule(\"") << Escaped(hxType.module) << TEXT("\")") << Newline();
  }
  // @:glueCppIncludes
  m_buf << TEXT("@:glueCppIncludes(\"") << Escaped(getHeaderPath(inClass->uclass->GetOuterUPackage(), inClass->header)) << TEXT("\")") << Newline();
  m_buf << TEXT("@:uextern extern ") << (isInterface ? TEXT("interface ") : TEXT("class ")) << hxType.name;
  if (!isInterface) {
    auto superUClass = uclass->GetSuperClass();
    const ClassDescriptor *super = nullptr;
    if (nullptr != superUClass) {
      super = m_haxeTypes.getDescriptor(superUClass);
      m_buf << " extends " << super->haxeType.toString();
    }
  }

  const auto implements = isInterface ? TEXT(" extends ") : TEXT(" implements ");
  // for now it doesn't seem that Unreal supports interfaces that extend other interfaces, but let's make ourselves ready for it
  for (auto& impl : uclass->Interfaces) {
    auto ifaceType = m_haxeTypes.getDescriptor(impl.Class);
    if (ifaceType->haxeType.kind != ETypeKind::KNone) {
      m_buf << implements << ifaceType->haxeType.toString();
    }
  }
  m_buf << Begin(TEXT(" {"));
  {
    if (!isNoExport) {
      this->generateFields(uclass);
    }
  }
  m_buf << End();
  printf("%s\n", TCHAR_TO_UTF8(*m_buf.toString()));
  return true;
}

void FHaxeGenerator::generateIncludeMetas(const NonClassDescriptor *inDesc) {
  m_buf << TEXT("@:glueCppIncludes(");
  auto first = true;
  for (auto& header : inDesc->getHeaders()) {
    if (first) first = false; else m_buf << TEXT(", ");
    m_buf << TEXT("\"") << Escaped(getHeaderPath(inDesc->module->getPackage(), header)) << TEXT("\"");
  }
  m_buf << TEXT(")") << Newline();
}

bool FHaxeGenerator::generateStruct(const StructDescriptor *inStruct) {
  auto hxType = inStruct->haxeType;
  m_buf << Comment(prelude);

  if (hxType.pack.Num() > 0) {
    m_buf << TEXT("package ") << FString::Join(hxType.pack, TEXT(".")) << ";" << Newline() << Newline();
  }
  
  auto ustruct = inStruct->ustruct;
  // comment
  bool isNoExport = (ustruct->StructFlags & STRUCT_NoExport) != 0;
  auto comment = ustruct->GetMetaData(NAME_ToolTip);
  if (isNoExport) {
    comment = TEXT("WARNING: This type is defined as NoExport by UHT. It will be empty because of it\n\n") + comment;
  }

  if (!comment.IsEmpty()) {
    m_buf << Comment(comment);
  }
  // @:umodule
  if (!hxType.module.IsEmpty()) {
    m_buf << TEXT("@:umodule(\"") << Escaped(hxType.module) << TEXT("\")") << Newline();
  }
  // @:glueCppIncludes
  generateIncludeMetas(inStruct);
  m_buf << TEXT("@:uextern extern ") << TEXT("class ") << hxType.name;

  auto superStruct = ustruct->GetSuperStruct();
  const StructDescriptor *super = nullptr;
  if (nullptr != superStruct) {
    super = m_haxeTypes.getDescriptor((UScriptStruct *) superStruct);
    if (nullptr != super) {
      m_buf << " extends " << super->haxeType.toString();
    }
  }
  m_buf << Begin(TEXT(" {"));
  {
    if (!isNoExport) {
      this->generateFields(ustruct);
    }
  }
  m_buf << End();
  printf("%s\n", TCHAR_TO_UTF8(*m_buf.toString()));
  return true;
}

bool FHaxeGenerator::generateEnum(const EnumDescriptor *inEnum) {
  auto uenum = inEnum->uenum;
  m_buf << Comment(prelude);
  auto hxType = inEnum->haxeType;
  if (hxType.pack.Num() > 0) {
    m_buf << TEXT("package ") << FString::Join(hxType.pack, TEXT(".")) << ";" << Newline() << Newline();
  }

  // comment
  auto& comment = uenum->GetMetaData(NAME_ToolTip);
  if (!comment.IsEmpty()) {
    m_buf << Comment(comment);
  }
  // @:umodule
  if (!hxType.module.IsEmpty()) {
    m_buf << TEXT("@:umodule(\"") << Escaped(hxType.module) << TEXT("\")") << Newline();
  }
  // @:glueCppIncludes
  generateIncludeMetas(inEnum);

  m_buf << TEXT("@:uname(\"") << Escaped(uenum->CppType.Replace(TEXT("::"), TEXT("."))) << TEXT("\")") << Newline();
  if (uenum->GetCppForm() == UEnum::ECppForm::EnumClass) {
    m_buf << TEXT("@:class ");
  }

  m_buf << TEXT("@:uextern extern ") << TEXT("enum ") << hxType.name;
  
  m_buf << Begin(TEXT(" {"));
  for (int i = 0; i < uenum->NumEnums() - 1; i++) {
    auto name = uenum->GetEnumName(i);
    auto ecomment = uenum->GetMetaData(FName(*(name + TEXT(".") + TEXT("ToolTip"))));
    auto displayName = uenum->GetMetaData(FName(*(name + TEXT(".") + TEXT("DisplayName"))));
    if (!displayName.IsEmpty()) {
      if (ecomment.IsEmpty()) {
        ecomment = displayName;
      } else {
        ecomment += TEXT("\n@DisplayName ") + displayName;
      }
    }
    if (!ecomment.IsEmpty()) {
      m_buf << Comment(ecomment);
    }

    if (!displayName.IsEmpty()) {
      m_buf << TEXT("@DisplayName(\"") << Escaped(displayName) << "\")" << Newline();
    }
    m_buf << name << TEXT(";") << Newline();
  }

  m_buf << End();

  printf("%s\n", TCHAR_TO_UTF8(*m_buf.toString()));
  // for (int32 enum_index = 0; enum_index < enum_p->NumEnums() - 1; ++enum_index)
  //   {
  //   FString enum_val_name = enum_p->GetEnumName(enum_index);
  //   FString enum_val_full_name = enum_p->GenerateFullEnumName(*enum_val_name);
  // LOG("ENUM %s -> %s", *uenum->GetName(), *uenum->GetPathName());
  // uenum->NumEnums
  return true;
}

bool FHaxeGenerator::writeWithModifiers(const FString &inName, UProperty *inProp, FString &outType) {
  if (inProp->ArrayDim > 1) {
    // TODO: support array dimensions (e.g. SomeType SomeProp[8])
    return false;
  }
  auto end = FString();
  // check all the flags that interest us
  // UStruct pointers aren't supported; so we're left either with PRef, PStruct and Const to check
  if (inProp->HasAnyPropertyFlags(CPF_ReturnParm)) {
    if (inProp->HasAnyPropertyFlags(CPF_ConstParm)) {
      outType += TEXT("unreal.Const<");
      end += TEXT(">");
    }
    if (inProp->HasAnyPropertyFlags(CPF_ReferenceParm)) {
      outType += TEXT("unreal.PRef<");
      end += TEXT(">");
    }
  } else {
    if (
      inProp->HasAnyPropertyFlags(CPF_ConstParm) ||
      (inProp->HasAnyPropertyFlags(CPF_ReferenceParm) && !inProp->HasAnyPropertyFlags(CPF_OutParm))
    ) {
      outType += TEXT("unreal.Const<");
      end += TEXT(">");
    }
    if (inProp->HasAnyPropertyFlags(CPF_ReferenceParm | CPF_OutParm)) {
      outType += TEXT("unreal.PRef<");
      end += TEXT(">");
    } 
  }

  // UHT bug: it doesn't provide any way to differentiate `const SomeType&` to `SomeType`
  // so we'll assume it's always the latter - which is more common
  // if (inProp->HasAnyPropertyFlags(CPF_Parm) && end.IsEmpty()) {
  //   outType += TEXT("unreal.Const<unreal.PRef<") + inName + TEXT(">>");
  //   return true;
  // }
  LOG("PROPERTY %s: %s %llx", *inName, *outType, (long long int) inProp->PropertyFlags);

  outType += inName + end;
  return true;
}

bool FHaxeGenerator::writeBasicWithModifiers(const FString &inName, UProperty *inProp, FString &outType) {
  // TODO support basic types' modifiers
  outType += inName;
  return true;
}

static bool canBuildTArrayProp(FString inInner) {
  // HACK: we need this since some types struggle with some operators (e.g. set operator)
  //       we'll need to find a better way to deal with this, but for now we'll just not include that into the built
  if (inInner.Contains(TEXT("FStaticMeshComponentLODInfo"), ESearchCase::CaseSensitive, ESearchDir::FromStart)) {
    return false;
  }
  return true;
}

bool FHaxeGenerator::upropType(UProperty* inProp, FString &outType) {
  // from the most common to the least
  if (inProp->IsA<UStructProperty>()) {
    auto prop = Cast<UStructProperty>(inProp);
    auto descr = m_haxeTypes.getDescriptor( prop->Struct );
    if (descr == nullptr) {
      LOG("(struct) TYPE NOT SUPPORTED: %s", *prop->Struct->GetName());
      // may happen if we never used this in a way the struct is known
      return false;
    }
    return writeWithModifiers(descr->haxeType.toString(), inProp, outType);
  } else if (inProp->IsA<UObjectProperty>()) {
    if (inProp->IsA<UClassProperty>()) {
      auto prop = Cast<UClassProperty>(inProp);
      if (prop->HasAnyPropertyFlags(CPF_UObjectWrapper)) {
        auto descr = m_haxeTypes.getDescriptor(prop->MetaClass);
        if (descr == nullptr) {
          LOG("(tsubclassof) TYPE NOT SUPPORTED: %s", *prop->PropertyClass->GetName());
          return false;
        }
        return writeWithModifiers(TEXT("unreal.TSubclassOf<") + descr->haxeType.toString() + TEXT(">"), inProp, outType);
      }
    }
    auto prop = Cast<UObjectProperty>(inProp);
    auto descr = m_haxeTypes.getDescriptor(prop->PropertyClass);
    if (descr == nullptr) {
      LOG("(uclass) TYPE NOT SUPPORTED: %s", *prop->PropertyClass->GetName());
      return false;
    }
    return writeWithModifiers(descr->haxeType.toString(), inProp, outType);
  } else if (inProp->IsA<UNumericProperty>()) {
    auto numeric = Cast<UNumericProperty>(inProp);
    UEnum *uenum = numeric->GetIntPropertyEnum();
    if (uenum != nullptr) {
      auto descr = m_haxeTypes.getDescriptor(uenum);
      if (descr == nullptr) {
        return false;
      }
      return writeWithModifiers(descr->haxeType.toString(), inProp, outType);
    }
    if (inProp->IsA<UByteProperty>()) {
      return writeBasicWithModifiers(TEXT("unreal.UInt8"), inProp, outType);
    } else if (inProp->IsA<UInt8Property>()) {
      return writeBasicWithModifiers(TEXT("unreal.Int8"), inProp, outType);
    } else if (inProp->IsA<UInt16Property>()) {
      return writeBasicWithModifiers(TEXT("unreal.Int16"), inProp, outType);
    } else if (inProp->IsA<UIntProperty>()) {
      return writeBasicWithModifiers(TEXT("unreal.Int32"), inProp, outType);
    } else if (inProp->IsA<UInt64Property>()) {
      return writeBasicWithModifiers(TEXT("unreal.Int64"), inProp, outType);
    } else if (inProp->IsA<UUInt16Property>()) {
      return writeBasicWithModifiers(TEXT("unreal.UInt16"), inProp, outType);
    } else if (inProp->IsA<UUInt32Property>()) {
      return writeBasicWithModifiers(TEXT("unreal.FakeUInt32"), inProp, outType);
    } else if (inProp->IsA<UUInt64Property>()) {
      return writeBasicWithModifiers(TEXT("unreal.FakeUInt64"), inProp, outType);
    } else if (inProp->IsA<UFloatProperty>()) {
      return writeBasicWithModifiers(TEXT("unreal.Float32"), inProp, outType);
    } else if (inProp->IsA<UDoubleProperty>()) {
      return writeBasicWithModifiers(TEXT("unreal.Float64"), inProp, outType);
    } else {
      LOG("NUMERIC TYPE NOT SUPPORTED: %s", *inProp->GetClass()->GetName());
      return false;
    }
    return true;
  } else if (inProp->IsA<UBoolProperty>()) {
    return writeBasicWithModifiers(TEXT("Bool"), inProp, outType);
    return true;
  } else if (inProp->IsA<UNameProperty>()) {
    return writeWithModifiers(TEXT("unreal.FName"), inProp, outType);
  } else if (inProp->IsA<UStrProperty>()) {
    return writeWithModifiers(TEXT("unreal.FString"), inProp, outType);
  } else if (inProp->IsA<UArrayProperty>()) {
    auto prop = Cast<UArrayProperty>(inProp);
    FString inner;
    if (!upropType(prop->Inner, inner))
      return false;
    return canBuildTArrayProp(inner) && writeWithModifiers(TEXT("unreal.TArray<") + inner + TEXT(">"), inProp, outType);
  }
  // uenum
  // uinterface
  // udelegate
  //
  // TLazyObjectPtr
  // UAssetObjectPtr - TPersistentObjectPtr
  // UInterfaceProperty
  // UMapProperty (TMap)
  // UDelegateProperty 
  // UMulticastDelegateProperty 

  LOG("Property %s (class %s) not supported", *inProp->GetName(), *inProp->GetClass()->GetName());
  return false;
}
