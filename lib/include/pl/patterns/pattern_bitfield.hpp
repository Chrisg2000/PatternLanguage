#pragma once

#include <pl/patterns/pattern.hpp>

namespace pl::ptrn {

    class PatternBitfieldField : public Pattern {
    public:
        PatternBitfieldField(core::Evaluator *evaluator, u64 offset, u8 bitOffset, u8 bitSize, Pattern *bitfieldPattern = nullptr)
            : Pattern(evaluator, offset, 0), m_bitOffset(bitOffset), m_bitSize(bitSize), m_bitField(bitfieldPattern) { }

        PatternBitfieldField(const PatternBitfieldField &other) : Pattern(other) {
            this->m_padding = other.m_padding;
            this->m_bitOffset = other.m_bitOffset;
            this->m_bitSize = other.m_bitSize;
            this->m_bitField = other.m_bitField;
        }

        [[nodiscard]] std::unique_ptr<Pattern> clone() const override {
            return std::unique_ptr<Pattern>(new PatternBitfieldField(*this));
        }

        [[nodiscard]] core::Token::Literal getValue() const override {
            std::vector<u8> value(this->m_bitField->getSize(), 0);
            this->getEvaluator()->readData(this->m_bitField->getOffset(), &value[0], value.size(), this->getSection());

            if (this->m_bitField->getEndian() != std::endian::native)
                std::reverse(value.begin(), value.end());

            return transformValue(u128(hlp::extract(this->m_bitOffset + (this->m_bitSize - 1), this->m_bitOffset, value)));
        }

        void setBitfield(Pattern *bitField) {
            this->m_bitField = bitField;
        }

        [[nodiscard]] std::string getFormattedName() const override {
            return this->m_bitSize == 1 ? "bit" : "bits";
        }

        [[nodiscard]] u8 getBitOffset() const {
            return this->m_bitOffset;
        }

        void setBitOffset(u8 bitOffset) {
            this->m_bitOffset = bitOffset;
        }

        [[nodiscard]] u8 getBitSize() const {
            return this->m_bitSize;
        }

        [[nodiscard]] bool operator==(const Pattern &other) const override {
            if (!compareCommonProperties<decltype(*this)>(other))
                return false;

            auto &otherBitfieldField = *static_cast<const PatternBitfieldField *>(&other);
            return this->m_bitOffset == otherBitfieldField.m_bitOffset && this->m_bitSize == otherBitfieldField.m_bitSize;
        }

        void accept(PatternVisitor &v) override {
            v.visit(*this);
        }

        std::string formatDisplayValue() override {
            auto literal = this->getValue();
            auto value =this->getValue().toUnsigned();

            return Pattern::formatDisplayValue(fmt::format("{0} (0x{1:X})", value, value), literal);
        }

        [[nodiscard]] std::string toString() const override {
            auto result = fmt::format("{}", this->getValue().toUnsigned());

            return Pattern::formatDisplayValue(result, this->getValue());
        }

        [[nodiscard]] bool isPadding() const { return this->m_padding; }
        void setPadding(bool padding) { this->m_padding = padding; }

    private:
        bool m_padding = false;
        u8 m_bitOffset, m_bitSize;
        Pattern *m_bitField = nullptr;
    };

    class PatternBitfield : public Pattern,
                            public Inlinable,
                            public Iteratable {
    public:
        PatternBitfield(core::Evaluator *evaluator, u64 offset, size_t size)
            : Pattern(evaluator, offset, size) { }

        PatternBitfield(const PatternBitfield &other) : Pattern(other) {
            for (auto &field : other.m_fields)
                this->m_fields.push_back(field->clone());
        }

        [[nodiscard]] std::unique_ptr<Pattern> clone() const override {
            return std::unique_ptr<Pattern>(new PatternBitfield(*this));
        }

        void setOffset(u64 offset) override {
            for (auto &field : this->m_fields)
                field->setOffset(field->getOffset() - this->getOffset() + offset);

            Pattern::setOffset(offset);
        }

        void setSection(u64 id) override {
            for (auto &field : this->m_fields)
                if (field->getSection() == ptrn::Pattern::MainSectionId)
                    field->setSection(id);

            Pattern::setSection(id);
        }

        [[nodiscard]] std::vector<std::pair<u64, Pattern*>> getChildren() override {
            if (this->isSealed()) {
                return { { this->getOffset(), this } };
            } else {
                std::vector<std::pair<u64, Pattern*>> result;

                for (const auto &field : this->m_fields) {
                    result.emplace_back(field->getOffset(), field.get());
                }

                return result;
            }
        }

        void setLocal(bool local) override {
            for (auto &pattern : this->m_fields)
                pattern->setLocal(local);

            Pattern::setLocal(local);
        }

        void setReference(bool reference) override {
            for (auto &pattern : this->m_fields)
                pattern->setReference(reference);

            Pattern::setReference(reference);
        }

        [[nodiscard]] std::string getFormattedName() const override {
            return "bitfield " + Pattern::getTypeName();
        }

        [[nodiscard]] std::vector<std::shared_ptr<Pattern>> getEntries() override {
            return this->m_fields;
        }

        void setFields(std::vector<std::shared_ptr<Pattern>> fields) {
            this->m_fields = std::move(fields);

            for (auto &field : this->m_fields) {
                field->setSize(this->getSize());
                field->setColor(this->getColor());
                field->setEndian(this->getEndian());
            }

            if (!this->m_fields.empty())
                this->setBaseColor(this->m_fields.front()->getColor());

            for (const auto &field : this->m_fields)
                this->m_sortedFields.push_back(field.get());
        }

        void setColor(u32 color) override {
            Pattern::setColor(color);
            for (auto &field : this->m_fields)
                field->setColor(color);
        }

        [[nodiscard]] bool operator==(const Pattern &other) const override {
            if (!compareCommonProperties<decltype(*this)>(other))
                return false;

            auto &otherBitfield = *static_cast<const PatternBitfield *>(&other);
            if (this->m_fields.size() != otherBitfield.m_fields.size())
                return false;

            for (u64 i = 0; i < this->m_fields.size(); i++) {
                if (*this->m_fields[i] != *otherBitfield.m_fields[i])
                    return false;
            }

            return true;
        }

        void accept(PatternVisitor &v) override {
            v.visit(*this);
        }

        [[nodiscard]] std::string toString() const override {
            std::string result = this->getFormattedName();

            result += " { ";

            for (const auto &field : this->m_fields) {
                if (field->getVariableName().starts_with("$"))
                    continue;

                result += fmt::format("{} = {}, ", field->getVariableName(), field->toString());
            }

            // Remove trailing ", "
            result.pop_back();
            result.pop_back();

            result += " }";

            return Pattern::formatDisplayValue(result, this->clone().get());
        }

        std::string formatDisplayValue() override {
            std::vector<u8> bytes(this->getSize(), 0);
            this->getEvaluator()->readData(this->getOffset(), bytes.data(), bytes.size(), this->getSection());

            if (this->getEndian() == std::endian::little)
                std::reverse(bytes.begin(), bytes.end());

            std::string valueString;

            for (const auto &pattern : this->m_fields) {
                auto field = static_cast<PatternBitfieldField *>(pattern.get());

                auto fieldValue = field->getValue().toUnsigned();

                if (fieldValue > 0) {
                    if (field->getBitSize() == 1)
                        valueString += fmt::format("{} | ", field->getVariableName());
                    else
                        valueString += fmt::format("{}({}) | ", field->getVariableName(), field->toString());
                }
            }

            if (valueString.size() >= 3) {
                valueString.pop_back();
                valueString.pop_back();
                valueString.pop_back();
            }

            return Pattern::formatDisplayValue(fmt::format("{{ {} }}", valueString), this);
        }

        void setEndian(std::endian endian) override {
            if (this->isLocal()) return;

            Pattern::setEndian(endian);

            for (auto &field : this->m_fields)
                field->setEndian(endian);
        }

        std::shared_ptr<Pattern> getEntry(size_t index) const override {
            return this->m_fields[index];
        }

        size_t getEntryCount() const override {
            return this->m_fields.size();
        }

        void forEachEntry(u64 start, u64 end, const std::function<void (u64, Pattern *)> &fn) override {
            if (this->isSealed())
                return;

            for (auto i = start; i < end; i++) {
                auto pattern = this->m_sortedFields[i];
                if (!pattern->isPatternLocal())
                    fn(i, pattern);
            }
        }

        void sort(const std::function<bool (const Pattern *, const Pattern *)> &comparator) override {
            this->m_sortedFields.clear();
            for (auto &member : this->m_fields)
                this->m_sortedFields.push_back(member.get());

            std::sort(this->m_sortedFields.begin(), this->m_sortedFields.end(), comparator);
        }

    private:
        std::vector<std::shared_ptr<Pattern>> m_fields;
        std::vector<Pattern *> m_sortedFields;
    };

}