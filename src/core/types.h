#ifndef TYPES_H
#define TYPES_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>

struct CatalogItem
{
    QString item;
    QString descr;
    QString knowledge;
};

struct ChatMessage
{
    QString role;    // "user" | "assistant"
    QString content;
};

struct DialogTurn
{
    QString speaker; // "seller" | "buyer"
    QString text;
};

struct ScoreSet
{
    int contact = 0;
    int needs = 0;
    int objections = 0;
    int offer = 0;
    int buyerFit = 0;

    QString contactMistake;
    QString contactRecommendation;
    QString needsMistake;
    QString needsRecommendation;
    QString objectionsMistake;
    QString objectionsRecommendation;
    QString offerMistake;
    QString offerRecommendation;
    QString buyerFitMistake;
    QString buyerFitRecommendation;

    double average() const
    {
        return (contact + needs + objections + offer + buyerFit) / 5.0;
    }

    int valueAt(int i) const
    {
        const int v[] = {contact, needs, objections, offer, buyerFit};
        return (i >= 0 && i < 5) ? v[i] : 0;
    }

    QString mistakeAt(int i) const
    {
        const QString v[] = {contactMistake, needsMistake, objectionsMistake,
                             offerMistake, buyerFitMistake};
        return (i >= 0 && i < 5) ? v[i] : QString();
    }

    QString recommendationAt(int i) const
    {
        const QString v[] = {contactRecommendation, needsRecommendation,
                             objectionsRecommendation, offerRecommendation,
                             buyerFitRecommendation};
        return (i >= 0 && i < 5) ? v[i] : QString();
    }

    void setMistakeAt(int i, const QString &text)
    {
        QString *v[] = {&contactMistake, &needsMistake, &objectionsMistake,
                        &offerMistake, &buyerFitMistake};
        if (i >= 0 && i < 5)
            *v[i] = text;
    }

    void setRecommendationAt(int i, const QString &text)
    {
        QString *v[] = {&contactRecommendation, &needsRecommendation,
                        &objectionsRecommendation, &offerRecommendation,
                        &buyerFitRecommendation};
        if (i >= 0 && i < 5)
            *v[i] = text;
    }
};

struct AnalysisRecord
{
    int dialogId = 0;
    QString seriesId;
    int cycleIndex = 0;
    QString productItem;
    QString productDescr;
    QString buyerType;
    QString buyerDescr;
    QString sellerPrompt;
    QString sellerPromptTemplate;
    QVector<DialogTurn> transcript;
    int pairCount = 0;
    qint64 elapsedMs = 0;
    ScoreSet scores;
    double average = 0.0;
    QStringList mistakes;
    QStringList recommendations;
    QString newPrompt;
    double delta = 0.0;
    bool hasDelta = false;
};

struct GgufModelInfo
{
    QString fileName;
    QString absolutePath;
    qint64 sizeBytes = 0;
};

#endif // TYPES_H
