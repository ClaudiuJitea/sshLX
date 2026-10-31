// FindBar - the find box above a tab's terminal. Only the box: SessionWidget
// does the searching, and the matches light up in the terminal itself.
#pragma once

#include <QFrame>

class QCheckBox;
class QLabel;
class QLineEdit;

class FindBar : public QFrame {
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);
    void open();
    QString text() const;
    bool matchCase() const;
    void setResult(const QString &text);

signals:
    void queryChanged();
    void next();
    void previous();
    void closed();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    QLineEdit *m_edit;
    QCheckBox *m_case;
    QLabel *m_result;
};
