/**
 * @file parser.h
 * @brief Contains the definition of the Parser class for parsing tokens and generating intermediate
 * code.
 * @author Vishank Singh, https://github.com/VishankSingh
 */

#ifndef PARSER_H
#define PARSER_H

#include "common/diagnostic_severity.h"
#include "code_generator.h"
#include "errors.h"
#include "tokens.h"


#include <map>
#include <string>
#include <variant>
#include <vector>

namespace Kites
{
/**
 * @brief Represents a parse error with a specific line number and error message.
 */
struct ParseError
{
    /// Line number where the error occurred, 1-based. 0 = unknown.
    unsigned int line = 0;
    /// First column of the offending token, 1-based. 0 = unknown.
    unsigned int column = 0;
    /// One past the last column of the offending token, 1-based and exclusive.
    /// 0 = unknown. See the column convention in common/diagnostic_severity.h.
    unsigned int end_column = 0;
    /// How bad it is. Everything the parser records today is an Error.
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    /// What kind of problem it is.
    DiagnosticCategory category = DiagnosticCategory::Syntax;
    /// The error message.
    std::string message;

    /**
     * @brief Constructs a ParseError with only a line number.
     *
     * Kept unchanged so every existing recordError() call site and test still
     * compiles; the column span stays unknown (0) and consumers fall back to
     * highlighting the whole line.
     *
     * @param line The line number where the error occurred.
     * @param message The error message.
     */
    ParseError(unsigned int line, std::string message) : line(line), message(std::move(message))
    {
    }

    /**
     * @brief Constructs a ParseError carrying the offending token's span.
     *
     * @param line        1-based line number.
     * @param column      1-based first column of the token.
     * @param end_column  1-based column one past the token's last character.
     * @param message     The error message.
     * @param severity    Defaults to Error.
     * @param category    Defaults to Syntax.
     */
    ParseError(unsigned int line, unsigned int column, unsigned int end_column,
               std::string message, DiagnosticSeverity severity = DiagnosticSeverity::Error,
               DiagnosticCategory category = DiagnosticCategory::Syntax)
        : line(line), column(column), end_column(end_column), severity(severity),
          category(category), message(std::move(message))
    {
    }
};

/**
 * @brief Tracks parsing errors.
 */
struct ErrorTracker
{
    unsigned int count = 0;               ///< The total number of errors.
    std::vector<ParseError> parse_errors; ///< A list of parse errors.
    std::vector<std::variant<errors::SyntaxError, errors::UnexpectedTokenError,
                             errors::ImmediateOutOfRangeError, errors::MisalignedImmediateError,
                             errors::UnexpectedOperandError, errors::InvalidLabelRefError,
                             errors::LabelRedefinitionError,
                             errors::InvalidRegisterError>>
        all_errors; ///< A list of all errors, including syntax and semantic errors.
};

/**
 * @brief Stores data about a symbol.
 */
struct SymbolData
{
    uint64_t address;     ///< The address or instruction location of the symbol.
    uint64_t line_number; ///< The line number where the symbol is defined.
    bool isData;          ///< Indicates if the symbol represents data or code.
};

/**
 * @brief The Parser class is responsible for parsing tokens and generating intermediate code and
 * symbol tables.
 */
class Parser
{
  private:
    std::string filename_;               ///< The filename being parsed.
    std::vector<Token> tokens_;          ///< The list of tokens to parse.
    size_t pos_ = 0;                     ///< The current position in the token list.
    unsigned int instruction_index_ = 0; ///< The current instruction index.

    ErrorTracker errors_; ///< The error tracker instance.

    std::vector<std::variant<uint8_t, uint16_t, uint32_t, uint64_t, std::string, float, double>>
        data_buffer_; ///< The buffer for data directives.

    uint64_t data_index_ = 0; ///< The current index for data allocation.

    std::map<std::string, SymbolData>
        symbol_table_; ///< The symbol table mapping symbol names to their data.

    std::vector<unsigned int> back_patch_; ///< List of instructions requiring backpatching.
    std::vector<std::pair<ICUnit, bool>> intermediate_code_; ///< The generated intermediate code.

    std::map<unsigned int, unsigned int>
        instruction_number_line_number_mapping_; ///< Maps instruction numbers to line numbers.

    /**
     * @brief Returns the previous token in the token list.
     * @return The previous token.
     */
    Token prevToken();

    /**
     * @brief Returns the current token in the token list.
     * @return The current token.
     */
    Token currentToken();

    /**
     * @brief Moves to the next token and returns it.
     * @return The next token.
     */
    Token nextToken();

    /**
     * @brief Peeks ahead by n tokens without advancing the position.
     * @param n The number of tokens to peek ahead.
     * @return The nth token from the current position.
     */
    Token peekToken(int n);

    /**
     * @brief Skips the current line during parsing.
     */
    void skipCurrentLine();

    /**
     * @brief Records a parse error.
     * @param error The parse error to record.
     */
    void recordError(const ParseError &error);

    bool parse_O_GPR_C_GPR_C_GPR();
    bool parse_O_GPR_C_GPR_C_I();
    bool parse_O_GPR_C_I();
    bool parse_O_GPR_C_GPR_C_IL();
    bool parse_O_GPR_C_GPR_C_DL();
    bool parse_O_GPR_C_IL();
    bool parse_O_GPR_C_DL();
    bool parse_O_GPR_C_I_LP_GPR_RP();
    bool parse_O();
    bool parse_pseudo();

    bool parse_O_GPR_C_CSR_C_GPR();
    bool parse_O_GPR_C_CSR_C_I();

    bool parse_O_FPR_C_FPR_C_FPR_C_FPR();
    bool parse_O_FPR_C_FPR_C_FPR_C_FPR_C_RM();
    bool parse_O_FPR_C_FPR_C_FPR();
    bool parse_O_FPR_C_FPR_C_FPR_C_RM();
    bool parse_O_FPR_C_FPR();
    bool parse_O_FPR_C_FPR_C_RM();

    bool parse_O_FPR_C_GPR();
    bool parse_O_FPR_C_GPR_C_RM();
    bool parse_O_GPR_C_FPR();
    bool parse_O_GPR_C_FPR_C_RM();
    bool parse_O_GPR_C_FPR_C_FPR();
    bool parse_O_FPR_C_I_LP_GPR_RP();

    /**
     * @brief Parses a data directive.
     */
    void parseDataDirective();

    /**
     * @brief Parses a text directive.
     */
    void parseTextDirective();

    /**
     * @brief Parses a bss directive.
     */
    void parseBSSDirective();

  public:
    /**
     * @brief Constructs a Parser instance.
     * @param filename The name of the file to parse.
     * @param tokens The list of tokens to parse.
     */
    explicit Parser(std::string filename, const std::vector<Token> &tokens)
        : filename_(std::move(filename)), tokens_(tokens)
    {
    }

    ~Parser() = default;

    /**
     * @brief Parses the tokens to generate intermediate code and symbol tables.
     */
    void parse();

    unsigned int getErrorCount() const;

    /**
     * @brief Returns the list of parse errors.
     * @return A const reference to the vector of parse errors.
     */
    [[nodiscard]] const std::vector<ParseError> &getErrors() const;

    /**
     * @brief Returns the data buffer.
     * @return A reference to the data buffer.
     */
    std::vector<std::variant<uint8_t, uint16_t, uint32_t, uint64_t, std::string, float, double>> &
    getDataBuffer();

    /**
     * @brief Returns the generated intermediate code.
     * @return A const reference to the intermediate code vector.
     */
    [[nodiscard]] const std::vector<std::pair<ICUnit, bool>> &getIntermediateCode() const;

    [[nodiscard]] const std::map<unsigned int, unsigned int> &
    getInstructionNumberLineNumberMapping() const;

    [[nodiscard]] const std::map<std::string, SymbolData> &getSymbolTable() const;

    /**
     * @brief Prints the list of errors to the console.
     */
    void printErrors() const;

    /**
     * @brief Prints the symbol table to the console.
     */
    void printSymbolTable() const;

    /**
     * @brief Prints the data buffers to the console.
     */
    void printDataBuffers() const;

    /**
     * @brief Prints the intermediate code to the console.
     */
    void printIntermediateCode() const;
};
}//namespace Kites
#endif // PARSER_H
