CHCP 65001

SET PYTHON="%USERPROFILE%\AppData\Local\Programs\Python\Python37\python.exe"

SET ENG_DATA="C:\Temp\riyuexingchen_gujian\Data\DBTables\data.table"
SET CHS_DATA="%USERPROFILE%\Downloads\GuJian Resources\data_tables\data.table"

SET GJDSEC=".\GJDSEC\GJDSEC.exe"
SET EXPORT_CONFIGS=.\GJDSEC
SET PARALLEL_TEXT=.\parallel_text

MKDIR temp
MKDIR %PARALLEL_TEXT%

FOR %%X IN (M01,Q01,M02,M03,Q02,Q04,Q05,Q12,M04,M05) DO (
  ECHO Processing %%X
  %GJDSEC% -f %ENG_DATA% -x "%EXPORT_CONFIGS%\SelectSeqDlg%%X.xml" > temp\SeqDlg%%X_eng.xml
  %GJDSEC% -f %CHS_DATA% -x "%EXPORT_CONFIGS%\SelectSeqDlg%%X.xml" > temp\SeqDlg%%X_chs.xml
  %PYTHON% CombineIntoParallelText.py temp\SeqDlg%%X_eng.xml temp\SeqDlg%%X_chs.xml %PARALLEL_TEXT%\SeqDlg%%X.txt
)