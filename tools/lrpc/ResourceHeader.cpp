#include "lrpc/ResourceHeader.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace loka
{
  namespace lrpc
  {
    namespace
    {
      typedef std::vector<std::string> SymbolPath;

      struct HeaderSymbol
      {
        SymbolPath path;
        const ManifestAsset *asset;
      };

      struct HeaderGroup
      {
        SymbolPath parent;
        std::vector<const HeaderSymbol *> symbols;
      };

      struct SymbolIdLess
      {
        bool operator()(const HeaderSymbol *left, const HeaderSymbol *right) const
        {
          if (left->asset->id != right->asset->id)
          {
            return left->asset->id < right->asset->id;
          }
          return left->path.back() < right->path.back();
        }
      };

      struct SymbolPathLess
      {
        bool operator()(const HeaderSymbol *left, const HeaderSymbol *right) const
        {
          return left->path < right->path;
        }
      };

      bool IsIdentifierStart(char c)
      {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
      }

      bool IsIdentifierRest(char c)
      {
        return IsIdentifierStart(c) || (c >= '0' && c <= '9') || c == '_';
      }

      bool IsKeyword(const std::string &value)
      {
        static const char *const keywords[] = {
            "alignas",     "alignof",  "and",        "and_eq",    "asm",       "auto",         "bitand",
            "bitor",       "bool",     "break",      "case",      "catch",     "char",         "char8_t",
            "char16_t",    "char32_t", "class",      "co_await",  "co_return", "co_yield",     "compl",
            "concept",     "const",    "const_cast", "consteval", "constexpr", "constinit",    "continue",
            "decltype",    "default",  "delete",     "do",        "double",    "dynamic_cast", "else",
            "enum",        "explicit", "export",     "extern",    "false",     "float",        "for",
            "friend",      "goto",     "if",         "inline",    "int",       "long",         "mutable",
            "namespace",   "new",      "noexcept",   "not",       "not_eq",    "nullptr",      "operator",
            "or",          "or_eq",    "private",    "protected", "public",    "register",     "reinterpret_cast",
            "requires",    "return",   "short",      "signed",    "sizeof",    "static",       "static_assert",
            "static_cast", "struct",   "switch",     "template",  "this",      "thread_local", "throw",
            "true",        "try",      "typedef",    "typeid",    "typename",  "union",        "unsigned",
            "using",       "virtual",  "void",       "volatile",  "wchar_t",   "while",        "xor",
            "xor_eq"};
        for (std::size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i)
        {
          if (value == keywords[i])
          {
            return true;
          }
        }
        return false;
      }

      bool IsGeneratedMemberName(const std::string &value)
      {
        return value == "AssetRef" || value == "AssetId" || value == "BagIndex" || value == "AssetKind"
               || value == "IdSpaceStamp" || value == "AssetCount" || value == "BagCount" || value == "Assets";
      }

      ResourceHeaderResult ParseSymbolPath(const std::string &name, SymbolPath &out)
      {
        out.clear();
        std::string segment;
        for (std::size_t i = 0; i <= name.size(); ++i)
        {
          if (i < name.size() && name[i] != '/')
          {
            segment.push_back(name[i]);
            continue;
          }
          if (segment.empty() || !IsIdentifierStart(segment[0]))
          {
            return RESOURCE_HEADER_BAD_SYMBOL;
          }
          for (std::size_t c = 1; c < segment.size(); ++c)
          {
            if (!IsIdentifierRest(segment[c]))
            {
              return RESOURCE_HEADER_BAD_SYMBOL;
            }
          }
          if (IsKeyword(segment))
          {
            return RESOURCE_HEADER_BAD_SYMBOL;
          }
          if (segment.find("__") != std::string::npos)
          {
            return RESOURCE_HEADER_BAD_SYMBOL;
          }
          if (IsGeneratedMemberName(segment))
          {
            return RESOURCE_HEADER_RESERVED_SYMBOL;
          }
          out.push_back(segment);
          segment.clear();
        }
        return out.empty() ? RESOURCE_HEADER_BAD_SYMBOL : RESOURCE_HEADER_OK;
      }

      bool IsPrefix(const SymbolPath &left, const SymbolPath &right)
      {
        if (left.size() > right.size())
        {
          return false;
        }
        for (std::size_t i = 0; i < left.size(); ++i)
        {
          if (left[i] != right[i])
          {
            return false;
          }
        }
        return true;
      }

      const char *KindName(core::resource::lrpk::AssetKind kind)
      {
        switch (kind)
        {
        case core::resource::lrpk::ASSET_KIND_UNKNOWN:
          return "ASSET_KIND_UNKNOWN";
        case core::resource::lrpk::ASSET_KIND_IMAGE:
          return "ASSET_KIND_IMAGE";
        case core::resource::lrpk::ASSET_KIND_STRING:
          return "ASSET_KIND_STRING";
        case core::resource::lrpk::ASSET_KIND_AUDIO:
          return "ASSET_KIND_AUDIO";
        }
        return "ASSET_KIND_UNKNOWN";
      }

      void AppendIndent(std::string &out, std::size_t depth)
      {
        out.append(depth * 2, ' ');
      }

      void AppendAssetValue(std::string &out, const ManifestAsset &asset)
      {
        char numbers[96];
        ::snprintf(numbers,
                   sizeof(numbers),
                   "{%luUL, %lu, loka::core::resource::lrpk::%s}",
                   static_cast<unsigned long>(asset.id),
                   static_cast<unsigned long>(asset.bag),
                   KindName(asset.kind));
        out += numbers;
      }

      void OpenNamespaces(std::string &out, const SymbolPath &path)
      {
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
          AppendIndent(out, i + 1);
          out += "namespace " + path[i] + "\n";
          AppendIndent(out, i + 1);
          out += "{\n";
        }
      }

      void CloseNamespaces(std::string &out, const SymbolPath &path)
      {
        for (std::size_t i = path.size(); i > 1; --i)
        {
          AppendIndent(out, i - 1);
          out += "} // namespace " + path[i - 2] + "\n";
        }
      }

      void AppendSymbolGroup(std::string &out, const HeaderGroup &group)
      {
        const SymbolPath &path = group.symbols[0]->path;
        OpenNamespaces(out, path);
        const std::size_t depth = path.size();
        for (std::size_t i = 0; i < group.symbols.size(); ++i)
        {
          AppendIndent(out, depth);
          out += "const AssetRef " + group.symbols[i]->path.back() + " = ";
          AppendAssetValue(out, *group.symbols[i]->asset);
          out += ";\n";
        }
        out += "\n";
        AppendIndent(out, depth);
        out += "const AssetRef Assets[] = {\n";
        for (std::size_t i = 0; i < group.symbols.size(); ++i)
        {
          AppendIndent(out, depth + 1);
          AppendAssetValue(out, *group.symbols[i]->asset);
          out += i + 1 == group.symbols.size() ? "\n" : ",\n";
        }
        AppendIndent(out, depth);
        out += "};\n";
        char count[64];
        ::snprintf(count,
                   sizeof(count),
                   "const std::size_t AssetCount = %lu;\n",
                   static_cast<unsigned long>(group.symbols.size()));
        AppendIndent(out, depth);
        out += count;
        CloseNamespaces(out, path);
        out += "\n";
      }
    } // namespace

    ResourceHeaderResult GenerateResourceHeader(const PackManifest &manifest, std::string &out)
    {
      std::vector<HeaderSymbol> symbols(manifest.assets.size());
      for (std::size_t i = 0; i < manifest.assets.size(); ++i)
      {
        const ResourceHeaderResult parsed = ParseSymbolPath(manifest.assets[i].name, symbols[i].path);
        if (parsed != RESOURCE_HEADER_OK)
        {
          return parsed;
        }
        if (symbols[i].path.size() < 2)
        {
          return RESOURCE_HEADER_NEEDS_NAMESPACE;
        }
        symbols[i].asset = &manifest.assets[i];
      }
      std::vector<const HeaderSymbol *> symbolsByPath;
      symbolsByPath.reserve(symbols.size());
      for (std::size_t i = 0; i < symbols.size(); ++i)
      {
        symbolsByPath.push_back(&symbols[i]);
      }
      std::sort(symbolsByPath.begin(), symbolsByPath.end(), SymbolPathLess());
      for (std::size_t i = 1; i < symbolsByPath.size(); ++i)
      {
        if (IsPrefix(symbolsByPath[i - 1]->path, symbolsByPath[i]->path))
        {
          return RESOURCE_HEADER_SYMBOL_COLLISION;
        }
      }

      std::vector<HeaderGroup> groups;
      for (std::size_t i = 0; i < symbolsByPath.size(); ++i)
      {
        SymbolPath parent(symbolsByPath[i]->path.begin(), symbolsByPath[i]->path.end() - 1);
        if (groups.empty() || groups.back().parent != parent)
        {
          HeaderGroup next;
          next.parent = parent;
          groups.push_back(next);
        }
        groups.back().symbols.push_back(symbolsByPath[i]);
      }
      for (std::size_t i = 0; i < groups.size(); ++i)
      {
        std::sort(groups[i].symbols.begin(), groups[i].symbols.end(), SymbolIdLess());
      }
      std::string generated;
      generated += "// Generated by lrpc. Edit the manifest, not this file.\n"
                   "#ifndef LOKA_LRPC_GENERATED_R_HPP\n"
                   "#define LOKA_LRPC_GENERATED_R_HPP\n\n"
                   "#include <cstddef>\n\n"
                   "#include \"core/resource/lrpk/LrpkFormat.hpp\"\n\n"
                   "namespace R\n"
                   "{\n"
                   "  typedef loka::core::resource::lrpk::U32 AssetId;\n"
                   "  typedef std::size_t BagIndex;\n"
                   "  typedef loka::core::resource::lrpk::AssetKind AssetKind;\n\n"
                   "  /** One baked resource identity. All fields come from the same\n"
                   "      manifest record that lrpc wrote into ASSETS.LRP. */\n"
                   "  struct AssetRef\n"
                   "  {\n"
                   "    AssetId id;\n"
                   "    BagIndex bag;\n"
                   "    AssetKind kind;\n"
                   "  };\n\n";

      char facts[160];
      ::snprintf(facts,
                 sizeof(facts),
                 "  const AssetId IdSpaceStamp = %luUL;\n"
                 "  const std::size_t AssetCount = %lu;\n"
                 "  const std::size_t BagCount = %lu;\n\n",
                 static_cast<unsigned long>(DeriveIdSpaceStamp(manifest)),
                 static_cast<unsigned long>(manifest.assets.size()),
                 static_cast<unsigned long>(manifest.bags.size()));
      generated += facts;

      // The named constants remain the ordinary door; Assets[] is the explicit
      // enumeration door for concepts such as a page sequence. Its id order is
      // canonical, so a manifest-only reorder cannot change application
      // meaning without changing the package's id-space stamp.
      for (std::size_t i = 0; i < groups.size(); ++i)
      {
        AppendSymbolGroup(generated, groups[i]);
      }

      generated += "} // namespace R\n\n"
                   "#endif // LOKA_LRPC_GENERATED_R_HPP\n";
      out.swap(generated);
      return RESOURCE_HEADER_OK;
    }
  } // namespace lrpc
} // namespace loka
