#include <pl/pattern_language.hpp>
#include <pl/core/parser.hpp>
#include <pl/helpers/file.hpp>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <pl/core/ast/ast_node_array_variable_decl.hpp>
#include <pl/core/ast/ast_node_bitfield.hpp>
#include <pl/core/ast/ast_node_enum.hpp>
#include <pl/core/ast/ast_node_function_definition.hpp>
#include <pl/core/ast/ast_node_struct.hpp>
#include <pl/core/ast/ast_node_type_decl.hpp>
#include <pl/core/ast/ast_node_union.hpp>

namespace pl::cli::sub {

    namespace {

        std::string getTypeName(const core::ast::ASTNode *type) {
            if (auto builtinType = dynamic_cast<const core::ast::ASTNodeBuiltinType*>(type))
                return core::Token::getTypeName(builtinType->getType());
            else if (auto typeDecl = dynamic_cast<const core::ast::ASTNodeTypeDecl*>(type)) {
                if (typeDecl->getName().empty())
                    return getTypeName(typeDecl->getType().get());
                else
                    return typeDecl->getName();
            } else {
                return "???";
            }
        }

        std::string generateAttributes(const core::ast::Attributable *attributable) {
            const auto &attributes = attributable->getAttributes();
            if (attributes.empty())
                return "";

            std::string result = " [[";
            for (const auto &attribute : attributes) {
                result += attribute->getAttribute();

                result += ", ";
            }

            result.pop_back();
            result.pop_back();

            return result + "]]";
        }

        std::string generateTemplateParams(const core::ast::ASTNodeTypeDecl *type) {
            const auto &templateParams = type->getTemplateParameters();
            if (templateParams.empty())
                return "";

            std::string result = "<";
            for (const auto &templateParam : templateParams) {
                if (auto typeDecl = dynamic_cast<const core::ast::ASTNodeTypeDecl*>(templateParam.get()); typeDecl != nullptr)
                    result += typeDecl->getName();
                else if (auto lvalue = dynamic_cast<const core::ast::ASTNodeLValueAssignment*>(templateParam.get()); lvalue != nullptr)
                    result += fmt::format("auto {}", lvalue->getLValueName());
                else
                    continue;

                result += ", ";
            }

            result.pop_back();
            result.pop_back();

            return result + ">";
        }

        std::string generateTypeDocumentation(const std::string &name, const core::ast::ASTNodeTypeDecl *type) {
            if (auto typeDecl = dynamic_cast<core::ast::ASTNodeTypeDecl*>(type->getType().get())) {
                return fmt::format("```pat\nusing {}{} = {}{};\n```", name, generateTemplateParams(type), getTypeName(typeDecl), generateAttributes(typeDecl));
            } else if (dynamic_cast<core::ast::ASTNodeStruct*>(type->getType().get())) {
                return fmt::format("```pat\nstruct {}{} {{ ... }}{};\n```", name, generateTemplateParams(type), generateAttributes(type));
            } else if (dynamic_cast<core::ast::ASTNodeUnion*>(type->getType().get())) {
                return fmt::format("```pat\nunion {}{} {{ ... }}{};\n```", name, generateTemplateParams(type), generateAttributes(type));
            } else if (dynamic_cast<core::ast::ASTNodeBitfield*>(type->getType().get())) {
                return fmt::format("```pat\nbitfield {}{} {{ ... }}{};\n```", name, generateTemplateParams(type), generateAttributes(type));
            } else if (auto enumDecl = dynamic_cast<core::ast::ASTNodeEnum*>(type->getType().get())) {
                auto result = fmt::format("```pat\nenum {}{} : {} {{\n", name, generateTemplateParams(type), getTypeName(enumDecl->getUnderlyingType().get()));
                for (auto &[enumValueName, enumValues] : enumDecl->getEntries()) {
                    result += fmt::format("    {},\n", enumValueName);
                }

                result.pop_back();
                result.pop_back();

                return result + fmt::format("\n}}{};\n```", generateAttributes(type));
            } else {
                return "";
            }
        }

    }

    void addDocsSubcommand(CLI::App *app) {
        static std::vector<std::fs::path> includePaths;
        static std::vector<std::string> defines;

        static std::fs::path patternFilePath, outputFilePath;
        static bool hideImplementationDetails;

        auto subcommand = app->add_subcommand("docs");

        // Add command line arguments
        subcommand->add_option("-p,--pattern,PATTERN_FILE", patternFilePath, "Pattern file")->required()->check(CLI::ExistingFile);
        subcommand->add_option("-o,--output,OUTPUT_FILE", outputFilePath, "Output file")->required();
        subcommand->add_option("-I,--includes", includePaths, "Include file paths")->take_all()->check(CLI::ExistingDirectory);
        subcommand->add_option("-D,--define", defines, "Define a preprocessor macro")->take_all();
        subcommand->add_flag("-n,--noimpls", hideImplementationDetails, "Hide implementation details");

        subcommand->callback([] {

            // Create and configure Pattern Language runtime
            pl::PatternLanguage runtime;
            runtime.setDangerousFunctionCallHandler([&]() {
                return false;
            });

            for (const auto &define : defines)
                runtime.addDefine(define);

            runtime.setIncludePaths(includePaths);

            // Execute pattern file
            hlp::fs::File patternFile(patternFilePath, hlp::fs::File::Mode::Read);

            auto ast = runtime.parseString(patternFile.readString());
            if (!ast.has_value()) {
                auto error = runtime.getError().value();
                fmt::print("Pattern Error: {}:{} -> {}\n", error.line, error.column, error.message);
                std::exit(EXIT_FAILURE);
            }

            // Output documentation
            std::string documentation = fmt::format("# `{}`\n", patternFilePath.stem().string());
            {
                // Add global documentation
                for (auto comment : runtime.getInternals().parser->getGlobalDocComments()) {
                    comment = hlp::trim(comment);
                    if (comment.starts_with('*'))
                        comment = comment.substr(1);

                    documentation += fmt::format("**{}**\n", hlp::trim(comment));
                }

                {
                    std::string sectionContent;
                    for (const auto &[name, type] : runtime.getInternals().parser->getTypes()) {
                        if (!type->shouldDocument())
                            continue;
                        if (hideImplementationDetails && name.contains("impl::"))
                            continue;

                        sectionContent += fmt::format("### **{}**\n", name);

                        for (auto line : hlp::splitString(type->getDocComment(), "\n")) {
                            line = hlp::trim(line);
                            if (line.starts_with('*'))
                                line = line.substr(1);
                            line = hlp::trim(line);

                            if (line.starts_with('@')) {
                                line = line.substr(1);

                                if (line.starts_with("tparam ")) {
                                    line = line.substr(5);
                                    line = hlp::trim(line);

                                    if (line.empty())
                                        continue;

                                    auto paramName = hlp::splitString(line, " ")[0];
                                    sectionContent += fmt::format("- `<{}>`: {}\n", paramName, hlp::trim(line.substr(paramName.size())));
                                } else if (line.starts_with("internal ")) {
                                    goto skip_type;
                                }
                            } else {
                                sectionContent += line + "\n";
                            }
                        }

                        sectionContent += generateTypeDocumentation(hlp::splitString(name, "::").back(), type.get()) + "\n";

                        skip_type:;
                    }

                    if (!sectionContent.empty()) {
                        documentation += "\n\n## Types\n\n";
                        documentation += sectionContent;
                    }
                }


                {
                    std::string sectionContent;

                    for (const auto &node : *ast) {
                        if (!node->shouldDocument())
                            continue;

                        if (auto *functionDecl = dynamic_cast<core::ast::ASTNodeFunctionDefinition *>(node.get()); functionDecl != nullptr) {
                            const auto &name = functionDecl->getName();
                            if (hideImplementationDetails && name.contains("impl::"))
                                continue;

                            sectionContent += fmt::format("### **{}**\n", name);

                            for (auto line : hlp::splitString(functionDecl->getDocComment(), "\n")) {
                                line = hlp::trim(line);
                                if (line.starts_with('*'))
                                    line = line.substr(1);
                                line = hlp::trim(line);

                                if (line.starts_with('@')) {
                                    line = line.substr(1);

                                    if (line.starts_with("param ")) {
                                        line = line.substr(5);
                                        line = hlp::trim(line);

                                        if (line.empty())
                                            continue;

                                        auto paramName = hlp::splitString(line, " ")[0];
                                        sectionContent += fmt::format("- `{}`: {}\n", paramName, hlp::trim(line.substr(paramName.size())));
                                    } else if (line.starts_with("return ")) {
                                        line = line.substr(6);
                                        line = hlp::trim(line);

                                        sectionContent += fmt::format("- `return`: {}\n", line);
                                    } else if (line.starts_with("internal ")) {
                                        goto skip_function;
                                    }
                                } else {
                                    sectionContent += line + "\n";
                                }
                            }

                            sectionContent += "\n```pat\n";
                            sectionContent += fmt::format("fn {}(", hlp::splitString(functionDecl->getName(), "::").back());

                            const auto &params = functionDecl->getParams();
                            for (const auto &[paramName, paramType] : params) {
                                std::string typeName = getTypeName(paramType.get());

                                sectionContent += fmt::format("{} {}, ", typeName, paramName);
                            }

                            if (auto paramPack = functionDecl->getParameterPack(); paramPack.has_value()) {
                                sectionContent += fmt::format("auto ... {}, ", paramPack.value());
                            }

                            if (!params.empty()) {
                                sectionContent.pop_back();
                                sectionContent.pop_back();
                            }

                            sectionContent += ");\n```\n";
                        }

                        skip_function:;
                    }

                    if (!sectionContent.empty()) {
                        documentation += "\n\n## Functions\n\n";
                        documentation += sectionContent;
                    }
                }
            }

            hlp::fs::createDirectories(outputFilePath.parent_path());
            hlp::fs::File outputFile(outputFilePath, hlp::fs::File::Mode::Create);
            outputFile.write(documentation);
        });
    }

}