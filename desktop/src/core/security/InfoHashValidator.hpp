#pragma once

#include <QtCore/QString>
#include <QtCore/QRegularExpression>

namespace Murmur {

/**
 * @brief Strict validator for BitTorrent info hashes
 * 
 * Ensures info hashes are exactly 40 hexadecimal characters (SHA-1 format),
 * which is required for proper database integrity and torrent identification.
 */
class InfoHashValidator {
public:
    /**
     * @brief Validates that a string is a proper BitTorrent info hash
     * @param infoHash The info hash string to validate
     * @return true if the hash is exactly 40 hexadecimal characters, false otherwise
     */
    static bool isValid(const QString& infoHash);
    
    /**
     * @brief Validates and normalizes an info hash to lowercase
     * @param infoHash The info hash string to validate and normalize
     * @return Normalized hash if valid, empty string if invalid
     */
    static QString normalize(const QString& infoHash);
    
    /**
     * @brief Generates a valid test info hash for testing purposes
     * @param seed Optional seed for deterministic generation
     * @return A valid 40-character hex string
     */
    static QString generateTestHash(int seed = 0);

private:
    // Regex pattern for exact 40-character hex validation
    static const QRegularExpression HASH_PATTERN;
    
    // Constants
    static constexpr int HASH_LENGTH = 40;
};

} // namespace Murmur
