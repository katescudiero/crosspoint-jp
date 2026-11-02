#pragma once

#include <HalStorage.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CssStyle.h"

class CssSelectorUsage;

/**
 * Lightweight CSS parser for EPUB stylesheets
 *
 * Parses CSS files and extracts styling information relevant for e-ink display.
 * Uses a two-phase approach: first tokenizes the CSS content, then builds
 * a rule database that can be queried during HTML parsing.
 *
 * Supported selectors:
 *   - Element selectors: p, div, h1, etc.
 *   - Class selectors: .classname
 *   - Combined: element.classname
 *   - Grouped: selector1, selector2 { }
 *
 * Not supported (silently ignored):
 *   - Descendant/child selectors (except ".vrtl .x")
 *   - Pseudo-classes and pseudo-elements
 *   - Media queries (content is skipped)
 *   - @import, @font-face, etc.
 */
class CssParser {
 public:
  // Bump when CSS cache format or rules change; section caches are invalidated when this changes
  static constexpr uint8_t CSS_CACHE_VERSION = 8;

  // 画面デバッグ用の診断カウンタ（起動後の累計。SETTINGS.debugDisplay の読書画面表示が読む）。
  // 静的な uint32_t 2 つ（DRAM 8 バイト）だけで、ヒープは使わない。
  static uint32_t diagLowHeapSkips;     // resolveStyle が低ヒープで CSS を無視した回数
  static uint32_t diagCacheLoadedRules;  // 直近の loadFromCache で読み込めた規則数

  // 低ヒープで規則の登録を捨てたとき true。true のまま saveToCache() してはいけない。
  bool parseTruncated_ = false;

  explicit CssParser(std::string cachePath) : cachePath(std::move(cachePath)) {}
  ~CssParser() = default;

  // Non-copyable
  CssParser(const CssParser&) = delete;
  CssParser& operator=(const CssParser&) = delete;

  /**
   * Load and parse CSS from a file stream.
   * Can be called multiple times to accumulate rules from multiple stylesheets.
   * @param source Open file handle to read from
   * @return true if parsing completed (even if no rules found). Returns false
   *         if the stream was invalid or parsing stopped early due to low
   *         heap; rules collected so far remain loaded, but the result must
   *         not be persisted via saveToCache().
   */
  bool loadFromStream(FsFile& source);

  /**
   * Look up the style for an HTML element, considering tag name and class attributes.
   * Applies CSS cascade: element style < class style < element.class style
   *
   * @param tagName The HTML element name (e.g., "p", "div")
   * @param classAttr The class attribute value (may contain multiple space-separated classes)
   * @return Combined style with all applicable rules merged
   */
  /**
   * Parse-time filter: while set, loadFromStream() registers only rules whose tag/class can match the
   * documents summarised in `usage`. Generic publisher stylesheets (e.g. the Iwanami / EBPAJ templates)
   * define ~1000 selectors of which a book uses a few dozen; holding them all in the selector map at
   * once costs ~150 KB of heap and can hit the low-heap abort path. The pointer is not owned and must
   * outlive the parse; pass nullptr to clear.
   */
  void setParseUsageFilter(const CssSelectorUsage* usage) { parseUsage_ = usage; }

  [[nodiscard]] CssStyle resolveStyle(const std::string& tagName, const std::string& classAttr,
                                      bool vertical = false) const;

  /**
   * Parse an inline style attribute string.
   * @param styleValue The value of a style="" attribute
   * @return Parsed style properties
   */
  [[nodiscard]] static CssStyle parseInlineStyle(const std::string& styleValue);

  /**
   * Check if any rules have been loaded
   */
  [[nodiscard]] bool empty() const { return rulesBySelector_.empty(); }

  /**
   * Get count of loaded rule sets
   */
  [[nodiscard]] size_t ruleCount() const { return rulesBySelector_.size(); }

  /**
   * Clear all loaded rules
   */
  void clear() {
    rulesBySelector_.clear();
    parseTruncated_ = false;
  }

  /**
   * Check if CSS rules cache file exists
   */
  bool hasCache() const;

  /**
   * Delete CSS rules cache file exists
   */
  void deleteCache() const;

  /**
   * Save parsed CSS rules to a cache file.
   * @return true if cache was written successfully
   */
  bool saveToCache() const;

  /**
   * Load CSS rules from a cache file.
   * Clears any existing rules before loading.
   * @param usage Optional filter: only rules whose selector can match the
   *        scanned document are kept in RAM. Pass nullptr to load all rules.
   * @return true if cache was loaded successfully
   */
  bool loadFromCache(const CssSelectorUsage* usage = nullptr);

  /**
   * Check that the cache file exists and matches CSS_CACHE_VERSION without
   * materializing any rules in RAM. Removes a stale cache file (version
   * mismatch) just like loadFromCache().
   * @return true if a usable cache file is present
   */
  bool validateCache() const;

 private:
  const CssSelectorUsage* parseUsage_ = nullptr;
  // Storage: maps normalized selector -> style properties
  std::unordered_map<std::string, CssStyle> rulesBySelector_;

  std::string cachePath;

  // Internal parsing helpers
  void processRuleBlockWithStyle(const std::string& selectorGroup, const CssStyle& style);
  static CssStyle parseDeclarations(const std::string& declBlock);
  static void parseDeclarationIntoStyle(const std::string& decl, CssStyle& style, std::string& propNameBuf,
                                        std::string& propValueBuf);

  // Individual property value parsers
  static CssTextAlign interpretAlignment(const std::string& val);
  static CssFontStyle interpretFontStyle(const std::string& val);
  static CssFontWeight interpretFontWeight(const std::string& val);
  static CssTextDecoration interpretDecoration(const std::string& val);
  static CssLength interpretLength(const std::string& val);
  /** Returns true only when a numeric length was parsed (e.g. 2em, 50%). False for auto/inherit/initial. */
  static bool tryInterpretLength(const std::string& val, CssLength& out);

  // String utilities
  static std::string normalized(const std::string& s);
  static void normalizedInto(const std::string& s, std::string& out);
  static std::vector<std::string> splitOnChar(const std::string& s, char delimiter);
  static std::vector<std::string> splitWhitespace(const std::string& s);
};
