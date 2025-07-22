#include "src/core/security/InputValidator.hpp"
#include <iostream>
#include <QtCore/QCoreApplication>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    QString test1 = "%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n";
    QString test2 = "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s";
    QString test3 = "%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x";
    QString test4 = "AAAA%08x.%08x.%08x.%08x.%08x.%08x.%08x";
    
    std::cout << "Test 1 (%n repeated): " << Murmur::InputValidator::containsSuspiciousContent(test1) << std::endl;
    std::cout << "Test 2 (%s repeated): " << Murmur::InputValidator::containsSuspiciousContent(test2) << std::endl;
    std::cout << "Test 3 (%x repeated): " << Murmur::InputValidator::containsSuspiciousContent(test3) << std::endl;
    std::cout << "Test 4 (%08x pattern): " << Murmur::InputValidator::containsSuspiciousContent(test4) << std::endl;
    
    return 0;
}