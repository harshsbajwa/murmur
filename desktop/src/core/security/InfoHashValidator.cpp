#include "InfoHashValidator.hpp"
#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>

namespace Murmur {

// Static regex pattern for exact 40-character hex validation (case insensitive)
const QRegularExpression InfoHashValidator::HASH_PATTERN(R"(^[a-fA-F0-9]{40}$)");

bool InfoHashValidator::isValid(const QString& infoHash) {
    // Check exact length first for performance
    if (infoHash.length() != HASH_LENGTH) {
        return false;
    }
    
    // Validate with regex pattern
    return HASH_PATTERN.match(infoHash).hasMatch();
}

QString InfoHashValidator::normalize(const QString& infoHash) {
    if (!isValid(infoHash)) {
        return QString();
    }
    
    return infoHash.toLower();
}

QString InfoHashValidator::generateTestHash(int seed) {
    // Use seed for deterministic generation in tests
    if (seed == 0) {
        seed = QRandomGenerator::global()->bounded(1000000);
    }
    
    // Create a unique input string based on the seed
    QString input = QString("test_torrent_hash_%1").arg(seed);
    
    // Generate SHA-1 hash
    QByteArray hash = QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Sha1);
    
    // Convert to lowercase hex string (exactly 40 characters)
    QString hexHash = hash.toHex().toLower();
    
    // Double check our result is valid
    Q_ASSERT(hexHash.length() == HASH_LENGTH);
    Q_ASSERT(isValid(hexHash));
    
    return hexHash;
}

} // namespace Murmur
