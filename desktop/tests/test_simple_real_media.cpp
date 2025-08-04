#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>

#include "utils/TestUtils.hpp"
#include "../src/core/storage/StorageManager.hpp"
#include "../src/core/common/Expected.hpp"

using namespace Murmur;
using namespace Murmur::Test;

/**
 * @brief Simple test using real media files to validate core functionality
 * 
 * A minimal test that verifies the test infrastructure works with real media files.
 */
class TestSimpleRealMedia : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testRealMediaFilesExist();
    void testStorageManagerWithRealData();
    void testTestUtilsWithRealFiles();

private:
    std::unique_ptr<QTemporaryDir> tempDir_;
    std::unique_ptr<StorageManager> storageManager_;
    QString realVideoFile_;
    QString realAudioFile_;
};

void TestSimpleRealMedia::initTestCase() {
    TestUtils::initializeTestEnvironment();
    
    // Check for real sample files
    QString realVideo = TestUtils::getRealSampleVideoFile();
    QString realAudio = TestUtils::getRealSampleAudioFile();
    
    if (realVideo.isEmpty() || realAudio.isEmpty()) {
        QSKIP("Real sample media files not found in desktop/resources/tests/");
    }
    
    TestUtils::logMessage("Simple real media tests initialized");
}

void TestSimpleRealMedia::cleanupTestCase() {
    TestUtils::cleanupTestEnvironment();
}

void TestSimpleRealMedia::init() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    realVideoFile_ = TestUtils::getRealSampleVideoFile();
    realAudioFile_ = TestUtils::getRealSampleAudioFile();
    
    // Initialize storage manager
    storageManager_ = std::make_unique<StorageManager>(this);
    QString dbPath = QString("%1/simple_test_%2.db")
                    .arg(tempDir_->path())
                    .arg(QDateTime::currentMSecsSinceEpoch());
    auto initResult = storageManager_->initialize(dbPath);
    ASSERT_EXPECTED_VALUE(initResult);
}

void TestSimpleRealMedia::cleanup() {
    storageManager_.reset();
    tempDir_.reset();
}

void TestSimpleRealMedia::testRealMediaFilesExist() {
    TEST_SCOPE("testRealMediaFilesExist");
    
    // Verify real sample files exist and are accessible
    QVERIFY(!realVideoFile_.isEmpty());
    QVERIFY(!realAudioFile_.isEmpty());
    
    ASSERT_FILE_EXISTS(realVideoFile_);
    ASSERT_FILE_EXISTS(realAudioFile_);
    
    // Verify files have reasonable sizes
    QFileInfo videoInfo(realVideoFile_);
    QFileInfo audioInfo(realAudioFile_);
    
    QVERIFY(videoInfo.size() > 1024); // At least 1KB
    QVERIFY(audioInfo.size() > 1024); // At least 1KB
    
    TestUtils::logMessage(QString("Real video file: %1 (%2 bytes)")
                         .arg(videoInfo.fileName())
                         .arg(videoInfo.size()));
    TestUtils::logMessage(QString("Real audio file: %1 (%2 bytes)")
                         .arg(audioInfo.fileName())
                         .arg(audioInfo.size()));
}

void TestSimpleRealMedia::testStorageManagerWithRealData() {
    TEST_SCOPE("testStorageManagerWithRealData");
    
    // Create torrent record based on real media file
    TorrentRecord torrent;
    // Generate a proper 40-character hex info hash
    torrent.infoHash = QString("%1").arg(QRandomGenerator::global()->generate64(), 40, 16, QChar('0')).left(40);
    torrent.name = "Real Media Test";
    torrent.magnetUri = QString("magnet:?xt=urn:btih:%1&dn=Real+Media+Test").arg(torrent.infoHash);
    torrent.size = QFileInfo(realVideoFile_).size();
    torrent.dateAdded = QDateTime::currentDateTime();
    torrent.lastActive = QDateTime::currentDateTime();
    torrent.savePath = QFileInfo(realVideoFile_).dir().absolutePath();
    torrent.progress = 1.0;
    torrent.status = "completed";
    torrent.metadata = QJsonObject(); // Empty metadata
    torrent.files = QStringList(); // Empty file list
    torrent.seeders = 0;
    torrent.leechers = 0;
    torrent.downloaded = torrent.size;
    torrent.uploaded = 0;
    torrent.ratio = 0.0;
    
    // Test adding torrent
    auto addResult = storageManager_->addTorrent(torrent);
    ASSERT_EXPECTED_VALUE(addResult);
    
    // Test retrieving torrent
    auto getResult = storageManager_->getTorrent(torrent.infoHash);
    ASSERT_EXPECTED_VALUE(getResult);
    
    TorrentRecord retrieved = getResult.value();
    QCOMPARE(retrieved.infoHash, torrent.infoHash);
    QCOMPARE(retrieved.name, torrent.name);
    QCOMPARE(retrieved.size, torrent.size);
    
    // Create media record
    MediaRecord media;
    media.torrentHash = torrent.infoHash;
    media.filePath = realVideoFile_;
    media.originalName = QFileInfo(realVideoFile_).baseName();
    media.mimeType = "video/mp4";
    media.fileSize = QFileInfo(realVideoFile_).size();
    media.duration = 0; // Unknown duration
    media.width = 0;
    media.height = 0;
    media.frameRate = 0.0;
    media.videoCodec = "";
    media.audioCodec = "";
    media.hasTranscription = false;
    media.dateAdded = QDateTime::currentDateTime();
    media.lastPlayed = QDateTime();
    media.playbackPosition = 0;
    media.metadata = QJsonObject();
    
    // Test adding media
    auto mediaResult = storageManager_->addMedia(media);
    ASSERT_EXPECTED_VALUE(mediaResult);
    QString mediaId = mediaResult.value();
    
    // Test retrieving media
    auto getMediaResult = storageManager_->getMedia(mediaId);
    ASSERT_EXPECTED_VALUE(getMediaResult);
    
    MediaRecord retrievedMedia = getMediaResult.value();
    QCOMPARE(retrievedMedia.filePath, realVideoFile_);
    QCOMPARE(retrievedMedia.fileSize, QFileInfo(realVideoFile_).size());
    
    TestUtils::logMessage("Storage manager successfully handled real media data");
}

void TestSimpleRealMedia::testTestUtilsWithRealFiles() {
    TEST_SCOPE("testTestUtilsWithRealFiles");
    
    // Test real media file validation
    QVERIFY(TestUtils::validateRealMediaFile(realVideoFile_));
    QVERIFY(TestUtils::validateRealMediaFile(realAudioFile_));
    
    // Test creating test files based on real samples
    QString testVideo = TestUtils::createTestVideoFile(_testScope.getTempDirectory(), 5, "mp4");
    QString testAudio = TestUtils::createTestAudioFile(_testScope.getTempDirectory(), 3, "wav");
    
    // These should exist
    if (!testVideo.isEmpty()) {
        ASSERT_FILE_EXISTS(testVideo);
    }
    if (!testAudio.isEmpty()) {
        ASSERT_FILE_EXISTS(testAudio);
    }
    
    TestUtils::logMessage("Test utilities successfully work with real media files");
}

int runTestSimpleRealMedia(int argc, char** argv) {
    TestSimpleRealMedia test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_simple_real_media.moc"