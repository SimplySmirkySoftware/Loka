#ifndef LOKA_WIN32_FILE_PATH_TESTS_HPP
#define LOKA_WIN32_FILE_PATH_TESTS_HPP

void testWin32OpenReadAcceptsFullWidthPath();
void testWin32FileFromWidePathSurvivesToOpen();
void testWin32FileNameIsTheLastBackslashComponent();
void testWin32OpenWriteTruncateAcceptsFullWidthPath();

#endif // LOKA_WIN32_FILE_PATH_TESTS_HPP
