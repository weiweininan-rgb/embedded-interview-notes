#include "theme.h"

QString applicationStyleSheet()
{
    return QStringLiteral(R"QSS(
QMainWindow, QDialog { background: #F4F6F8; color: #20242A; }
QWidget { font-family: "Microsoft YaHei UI", "Segoe UI"; font-size: 14px; color: #20242A; }
QWidget#centralRoot { background: #F4F6F8; }
QFrame#appHeader { background: #B4232D; border: none; }
QLabel#appTitle { color: #FFFFFF; font-size: 21px; font-weight: 700; }
QLabel#appSubtitle { color: #F8DDE0; font-size: 12px; }
QWidget#controlPanel { background: transparent; }

QGroupBox {
    background: #FFFFFF;
    border: 1px solid #E2E6EA;
    border-radius: 12px;
    margin-top: 13px;
    padding: 16px 14px 13px 14px;
    font-weight: 650;
    color: #30353B;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 14px;
    padding: 0 7px;
    color: #8F1D27;
    background: #FFFFFF;
}
QLineEdit, QComboBox, QSpinBox {
    min-height: 34px;
    padding: 0 10px;
    background: #FAFBFC;
    border: 1px solid #D8DDE3;
    border-radius: 7px;
    selection-background-color: #B4232D;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
    background: #FFFFFF;
    border: 1px solid #B4232D;
}
QComboBox::drop-down, QSpinBox::up-button, QSpinBox::down-button { border: none; width: 24px; }
QComboBox QAbstractItemView {
    background: #FFFFFF;
    border: 1px solid #D8DDE3;
    selection-background-color: #F8E7E9;
    selection-color: #8F1D27;
    outline: none;
}

QPushButton {
    min-height: 35px;
    padding: 0 15px;
    background: #FFFFFF;
    border: 1px solid #CFD5DC;
    border-radius: 7px;
    font-weight: 600;
}
QPushButton:hover { background: #F8EDEF; border-color: #C65A64; color: #8F1D27; }
QPushButton:pressed { background: #F1D9DC; }
QPushButton:disabled { background: #EEF1F4; border-color: #E0E4E8; color: #9AA2AB; }
QPushButton[role="primary"] { background: #B4232D; border-color: #B4232D; color: #FFFFFF; }
QPushButton[role="primary"]:hover { background: #971B24; border-color: #971B24; color: #FFFFFF; }
QPushButton[role="primary"]:disabled { background: #E2E6EA; border-color: #E2E6EA; color: #929AA3; }
QPushButton[role="danger"] { background: #FFFFFF; border-color: #D87981; color: #9C202A; }

QLabel#connectionState[connected="true"] { color: #16803A; font-weight: 700; }
QLabel#connectionState[connected="false"] { color: #9B3B43; font-weight: 700; }
QLabel#serviceStatus { color: #5F6872; padding: 2px 0; }
QLabel#resultState { color: #8F1D27; font-weight: 700; }
QLabel[metric="true"] { color: #8F1D27; font-size: 17px; font-weight: 700; }

QProgressBar {
    min-height: 7px;
    max-height: 7px;
    border: none;
    border-radius: 3px;
    background: #E8EBEF;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk { background: #B4232D; border-radius: 3px; }

QTabWidget#chartTabs::pane {
    background: #FFFFFF;
    border: 1px solid #E2E6EA;
    border-radius: 12px;
    top: -1px;
}
QTabBar::tab {
    min-width: 76px;
    min-height: 36px;
    padding: 0 16px;
    background: #E9EDF1;
    border: none;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
    margin-right: 4px;
    font-weight: 650;
}
QTabBar::tab:selected { background: #B4232D; color: #FFFFFF; }
QTabBar::tab:hover:!selected { background: #F5DDE0; color: #8F1D27; }

QListWidget {
    background: #FAFBFC;
    border: 1px solid #E0E4E8;
    border-radius: 7px;
    padding: 5px;
    color: #555F69;
}
QToolTip { background: #30353B; color: #FFFFFF; border: none; padding: 6px; }
)QSS");
}
