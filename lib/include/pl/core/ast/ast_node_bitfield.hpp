#pragma once

#include <pl/core/ast/ast_node.hpp>
#include <pl/core/ast/ast_node_attribute.hpp>

#include <pl/patterns/pattern_bitfield.hpp>

namespace pl::core::ast {

    class ASTNodeBitfield : public ASTNode,
                            public Attributable {
    public:
        ASTNodeBitfield() : ASTNode() { }

        ASTNodeBitfield(const ASTNodeBitfield &other) : ASTNode(other), Attributable(other) {
            for (const auto &entry : other.getEntries())
                this->m_entries.emplace_back(entry->clone());
        }

        [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
            return std::unique_ptr<ASTNode>(new ASTNodeBitfield(*this));
        }

        [[nodiscard]] const std::vector<std::shared_ptr<ASTNode>> &getEntries() const { return this->m_entries; }
        void addEntry(std::unique_ptr<ASTNode> &&entry) { this->m_entries.emplace_back(std::move(entry)); }

        [[nodiscard]] std::vector<std::shared_ptr<ptrn::Pattern>> createPatterns(Evaluator *evaluator) const override {
            evaluator->updateRuntime(this);

            auto bitfieldPattern = std::make_shared<ptrn::PatternBitfield>(evaluator, evaluator->dataOffset(), 0);

            size_t bitOffset = 0x00;

            std::vector<std::shared_ptr<ptrn::Pattern>> fields;
            std::vector<std::shared_ptr<ptrn::Pattern>> potentialPatterns;

            BitfieldOrder order = evaluator->getBitfieldOrder();
            if (this->hasAttribute("left_to_right", false))
                order = BitfieldOrder::LeftToRight;
            else if (this->hasAttribute("right_to_left", false))
                order = BitfieldOrder::RightToLeft;

            std::vector<ASTNode *> entries;
            for (const auto &entry : this->m_entries)
                entries.emplace_back(entry.get());

            if (order == BitfieldOrder::LeftToRight)
                std::reverse(entries.begin(), entries.end());

            evaluator->pushScope(bitfieldPattern, potentialPatterns);
            PL_ON_SCOPE_EXIT {
                evaluator->popScope();
            };

            for (auto &entry : entries) {
                auto patterns = entry->createPatterns(evaluator);

                for (auto &pattern : patterns) {
                    if (auto bitfieldField = dynamic_cast<ptrn::PatternBitfieldField*>(pattern.get()); bitfieldField != nullptr) {
                        if (bitfieldField->getSize() == 0) {
                            bitfieldField->setBitOffset(bitOffset);
                            bitfieldField->setSection(evaluator->getSectionId());
                            bitOffset += bitfieldField->getBitSize();
                            bitfieldField->setSize((bitOffset + 7) / 8);
                        }

                        applyVariableAttributes(evaluator, entry, pattern);
                    }
                    bitfieldPattern->setSize((bitOffset + 7) / 8);
                }

                std::move(patterns.begin(), patterns.end(), std::back_inserter(potentialPatterns));

                if (!evaluator->getCurrentArrayIndex().has_value()) {
                    if (evaluator->getCurrentControlFlowStatement() == ControlFlowStatement::Return)
                        break;
                    else if (evaluator->getCurrentControlFlowStatement() == ControlFlowStatement::Break) {
                        evaluator->setCurrentControlFlowStatement(ControlFlowStatement::None);
                        break;
                    } else if (evaluator->getCurrentControlFlowStatement() == ControlFlowStatement::Continue) {
                        evaluator->setCurrentControlFlowStatement(ControlFlowStatement::None);
                        potentialPatterns.clear();
                        break;
                    }
                }
            }

            for (auto &pattern : potentialPatterns) {
                if (auto bitfieldField = dynamic_cast<ptrn::PatternBitfieldField*>(pattern.get()); bitfieldField != nullptr) {
                    bitfieldField->setBitfield(bitfieldPattern.get());
                    if (!bitfieldField->isPadding())
                        fields.push_back(std::move(pattern));
                }
            }

            bitfieldPattern->setFields(fields);

            evaluator->dataOffset() += bitfieldPattern->getSize();

            applyTypeAttributes(evaluator, this, bitfieldPattern);

            return hlp::moveToVector<std::shared_ptr<ptrn::Pattern>>(std::move(bitfieldPattern));
        }

    private:
        std::vector<std::shared_ptr<ASTNode>> m_entries;
    };

}