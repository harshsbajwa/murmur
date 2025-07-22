#include <QtTest/QtTest>
#include "../src/core/security/InputValidator.hpp"

using namespace Murmur;

class TestInputValidator : public QObject {
    Q_OBJECT
    
private slots:
    void testMagnetUriValidation() {
        // Valid magnet URI
        QString validMagnet = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678";
        QVERIFY(InputValidator::validateMagnetUri(validMagnet));
        
        // Invalid magnet URIs
        QVERIFY(!InputValidator::validateMagnetUri(""));
        QVERIFY(!InputValidator::validateMagnetUri("not-a-magnet"));
        QVERIFY(!InputValidator::validateMagnetUri("magnet:invalid"));
        QVERIFY(!InputValidator::validateMagnetUri("magnet:?xt=urn:btih:invalid-hash"));
    }
    
    void testFileNameValidation() {
        // Valid file names
        QVERIFY(InputValidator::validateFileName("video.mp4"));
        QVERIFY(InputValidator::validateFileName("My Movie (2023).mkv"));
        QVERIFY(InputValidator::validateFileName("test_file-123.avi"));
        
        // Invalid file names
        QVERIFY(!InputValidator::validateFileName(""));
        QVERIFY(!InputValidator::validateFileName("file<with>invalid|chars"));
        QVERIFY(!InputValidator::validateFileName("CON")); // Reserved name
        QVERIFY(!InputValidator::validateFileName("PRN.txt")); // Reserved name
    }
    
    void testFileSizeValidation() {
        // Valid sizes
        QVERIFY(InputValidator::validateFileSize(0));
        QVERIFY(InputValidator::validateFileSize(1024 * 1024)); // 1MB
        QVERIFY(InputValidator::validateFileSize(1024LL * 1024 * 1024)); // 1GB
        
        // Invalid sizes
        QVERIFY(!InputValidator::validateFileSize(-1));
        QVERIFY(!InputValidator::validateFileSize(100LL * 1024 * 1024 * 1024)); // 100GB (over limit)
    }
    
    void testPathTraversalDetection() {
        // Safe paths
        QVERIFY(!InputValidator::isPathTraversalAttempt("file.txt"));
        QVERIFY(!InputValidator::isPathTraversalAttempt("folder/file.txt"));
        
        // Path traversal attempts
        QVERIFY(InputValidator::isPathTraversalAttempt("../file.txt"));
        QVERIFY(InputValidator::isPathTraversalAttempt("folder/../../../etc/passwd"));
        QVERIFY(InputValidator::isPathTraversalAttempt("..\\windows\\system32"));
    }
    
    void testVideoFormatValidation() {
        // Valid formats
        QVERIFY(InputValidator::validateVideoFormat("mp4"));
        QVERIFY(InputValidator::validateVideoFormat("avi"));
        QVERIFY(InputValidator::validateVideoFormat("mkv"));
        QVERIFY(InputValidator::validateVideoFormat("MP4")); // Case insensitive
        
        // Invalid formats
        QVERIFY(!InputValidator::validateVideoFormat("txt"));
        QVERIFY(!InputValidator::validateVideoFormat("exe"));
        QVERIFY(!InputValidator::validateVideoFormat(""));
    }
    
    void testLanguageCodeValidation() {
        // Valid codes
        QVERIFY(InputValidator::validateLanguageCode("auto"));
        QVERIFY(InputValidator::validateLanguageCode("en"));
        QVERIFY(InputValidator::validateLanguageCode("es"));
        QVERIFY(InputValidator::validateLanguageCode("fr"));
        
        // Invalid codes
        QVERIFY(!InputValidator::validateLanguageCode(""));
        QVERIFY(!InputValidator::validateLanguageCode("invalid"));
        QVERIFY(!InputValidator::validateLanguageCode("xx"));
    }
    
    void testSuspiciousContentDetection() {
        // Safe content
        QVERIFY(!InputValidator::containsSuspiciousContent("Normal text content"));
        QVERIFY(!InputValidator::containsSuspiciousContent("Movie title (2023)"));
        
        // Suspicious content
        QVERIFY(InputValidator::containsSuspiciousContent("<script>alert('xss')</script>"));
        QVERIFY(InputValidator::containsSuspiciousContent("javascript:void(0)"));
        QVERIFY(InputValidator::containsSuspiciousContent("eval(malicious_code)"));
    }
    
    void testTextSanitization() {
        QString input = "<script>alert('test')</script>Normal content";
        QString sanitized = InputValidator::sanitizeText(input);
        
        QVERIFY(!sanitized.contains("<script>"));
        QVERIFY(sanitized.contains("Normal content"));
    }
};

int runTestInputValidator(int argc, char** argv) {
    TestInputValidator test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_input_validator.moc"