#include <QtTest/QtTest>
#include "../src/core/common/Expected.hpp"

using namespace Murmur;

class TestExpected : public QObject {
    Q_OBJECT
    
private slots:
    void testValueConstruction() {
        Expected<int, QString> result(42);
        
        QVERIFY(result.hasValue());
        QVERIFY(!result.hasError());
        QCOMPARE(result.value(), 42);
    }
    
    void testErrorConstruction() {
        Expected<int, QString> result(QString("Error occurred"));
        
        QVERIFY(!result.hasValue());
        QVERIFY(result.hasError());
        QCOMPARE(result.error(), QString("Error occurred"));
    }
    
    void testMonadicOperations() {
        Expected<int, QString> success(10);
        
        auto doubled = success.transform([](int x) { return x * 2; });
        QVERIFY(doubled.hasValue());
        QCOMPARE(doubled.value(), 20);
        
        Expected<int, QString> failure("Failed");
        auto failedTransform = failure.transform([](int x) { return x * 2; });
        QVERIFY(failedTransform.hasError());
        QCOMPARE(failedTransform.error(), QString("Failed"));
    }
    
    void testValueOr() {
        Expected<int, QString> success(42);
        QCOMPARE(success.valueOr(0), 42);
        
        Expected<int, QString> failure("Error");
        QCOMPARE(failure.valueOr(99), 99);
    }
    
    void testCopySemantics() {
        Expected<int, QString> original(123);
        Expected<int, QString> copy = original;
        
        QVERIFY(copy.hasValue());
        QCOMPARE(copy.value(), 123);
        
        // Original should still be valid
        QVERIFY(original.hasValue());
        QCOMPARE(original.value(), 123);
    }
};

int runTestExpected(int argc, char** argv) {
    TestExpected test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_expected.moc"