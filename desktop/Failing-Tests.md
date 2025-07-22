# Failing Tests Analysis

## Summary
Total failing test suites: **2 out of 9** (22% failure rate)
- **SecurityComponents**: 2 failures
- **EndToEndIntegration**: 6 failures

## 1. SecurityComponents Test Failures

### 1.1 `testResourceLimits()` - Line 472
**Error**: `resourceResult.hasValue()` returned FALSE

**Root Cause**: 
The test attempts to get resource usage from a sandbox that has already been destroyed.

**Stack Trace Analysis**:
- Line 467: `sandbox_->destroySandbox(sandboxId);` - Sandbox is destroyed
- Line 471: `auto resourceResult = sandbox_->getResourceUsage("test_resources");` - Attempts to get usage from destroyed sandbox
- The `getResourceUsage()` method in `SandboxManager.cpp:522` checks if sandbox exists and returns `ConfigurationError` if not found

**Fix Hypothesis**: 
Move the `getResourceUsage()` call before `destroySandbox()` or create a new sandbox for this specific test.

### 1.2 `testTimingAttacks()` - Line 1100
**Error**: `ratio < 1.5` returned FALSE with message "Timing difference too large: 0ms vs 0ms"

**Root Cause**: 
The timing measurement returns 0ms for both valid and invalid hash validations, causing a division by zero or invalid ratio calculation.

**Stack Trace Analysis**:
- Lines 1084-1100: The test measures timing for 1000 iterations of hash validation
- Both `validTime` and `invalidTime` are 0ms, indicating the operations complete too quickly to measure
- The ratio calculation `qMax(validTime, invalidTime) / qMin(validTime, invalidTime)` fails when both are 0

**Fix Hypothesis**: 
Increase iteration count, use more precise timing (microseconds), or add artificial delay to make timing differences measurable.

## 2. EndToEndIntegration Test Failures

All EndToEndIntegration failures follow the same pattern: `hasValue()` returned FALSE on various operations.

### 2.1 `testTorrentDownloadAndTranscriptionWorkflow()` - Line 271
**Error**: `addResult.hasValue()` returned FALSE

**Root Cause**: 
The `storage_->addTranscription(transcription)` call fails, likely due to missing or invalid transcription record data.

**Analysis**:
- TranscriptionRecord is created with basic fields but may be missing required database constraints
- The transcription.mediaId might be invalid or the database relationship constraints are failing

### 2.2 `testBatchProcessingWorkflow()` - Line 422  
**Error**: `result.hasValue()` returned FALSE

**Root Cause**: 
Similar to above - `storage_->addMedia(media)` operations are failing in the batch processing loop.

### 2.3 `testErrorRecoveryWorkflow()` - Line 461
**Error**: `storage_->addTorrent(newTorrent).hasValue()` returned FALSE

**Root Cause**: 
The hash validation is failing. The test attempts to fix a 42-character hash by truncating to 40 characters, but the validation logic may still be rejecting it.

**Analysis**:
- Line 452: `newTorrent.infoHash = "recovery1234567890abcdef1234567890abcdef12";` (42 chars)
- Line 460: `newTorrent.infoHash = "recovery1234567890abcdef1234567890abcdef1";` (41 chars) 
- The hash is still not exactly 40 characters as required

### 2.4 `testTranscriptionAndStorageIntegration()` - Line 508
**Error**: `mediaResult.hasValue()` returned FALSE

**Root Cause**: 
The `storage_->addMedia(media)` call fails before transcription testing even begins.

### 2.5 `testDataPersistenceAcrossRestarts()` - Line 575
**Error**: `mediaResult.hasValue()` returned FALSE

**Root Cause**: 
Same as above - media record creation fails, preventing persistence testing.

### 2.6 `testMetadataConsistencyWorkflow()` - Line 608
**Error**: `result.hasValue()` returned FALSE

**Root Cause**: 
Media record creation fails during the consistency testing loop.

## Common Root Cause Analysis

### Database/Storage Issues
Most EndToEndIntegration failures stem from database operations failing. Common causes:

1. **Hash Validation**: Many tests use malformed info hashes (wrong length)
2. **Foreign Key Constraints**: Media records referencing non-existent torrents
3. **Required Field Validation**: Missing or invalid required database fields
4. **Database Connection Issues**: Possible transaction or connection state problems

### Specific Issues Found:

#### Hash Generation Problems
- `createTestTorrent()` generates hashes that may not always be exactly 40 characters
- Hash concatenation logic: `baseHash + counterStr` may exceed 40 characters
- The `.left(40)` truncation may create invalid or duplicate hashes

#### Missing Field Validation
- MediaRecord and TranscriptionRecord may be missing required database fields
- Foreign key relationships may not be properly established

#### Database State Issues
- Tests may not properly clean up between runs
- Database constraints may be stricter than test data assumes

## Recommended Fixes

### SecurityComponents:
1. **testResourceLimits**: Move resource usage check before sandbox destruction
2. **testTimingAttacks**: Use microsecond timing or increase iteration count

### EndToEndIntegration:
1. **Hash Generation**: Fix `createTestTorrent()` to ensure exactly 40-character hashes
2. **Database Validation**: Add proper validation to ensure all required fields are set
3. **Constraint Checking**: Verify foreign key relationships before record creation
4. **Error Logging**: Add detailed error logging to identify specific database constraint violations

### General:
1. Add more detailed error reporting in Expected<> return values
2. Implement better test isolation and cleanup
3. Add database constraint verification in test setup

## Test Priority for Fixing
1. **High Priority**: Hash generation issues (affects multiple EndToEnd tests)
2. **Medium Priority**: Database constraint validation
3. **Low Priority**: SecurityComponents timing precision issues

## Log Evidence
The test log shows successful sandbox operations but no detailed database error messages, suggesting the failures occur at the application logic level rather than system level.
