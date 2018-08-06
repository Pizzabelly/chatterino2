#include "messages/MessageElement.hpp"

#include "Application.hpp"
#include "common/Emotemap.hpp"
#include "controllers/moderationactions/ModerationActions.hpp"
#include "debug/Benchmark.hpp"
#include "messages/layouts/MessageLayoutContainer.hpp"
#include "messages/layouts/MessageLayoutElement.hpp"
#include "singletons/Settings.hpp"
#include "util/DebugCount.hpp"

namespace chatterino {

MessageElement::MessageElement(Flags flags)
    : flags_(flags)
{
    DebugCount::increase("message elements");
}

MessageElement::~MessageElement()
{
    DebugCount::decrease("message elements");
}

MessageElement *MessageElement::setLink(const Link &link)
{
    this->link_ = link;
    return this;
}

MessageElement *MessageElement::setTooltip(const QString &tooltip)
{
    this->tooltip_ = tooltip;
    return this;
}

MessageElement *MessageElement::setTrailingSpace(bool value)
{
    this->trailingSpace = value;
    return this;
}

const QString &MessageElement::getTooltip() const
{
    return this->tooltip_;
}

const Link &MessageElement::getLink() const
{
    return this->link_;
}

bool MessageElement::hasTrailingSpace() const
{
    return this->trailingSpace;
}

MessageElement::Flags MessageElement::getFlags() const
{
    return this->flags_;
}

// IMAGE
ImageElement::ImageElement(ImagePtr image, MessageElement::Flags flags)
    : MessageElement(flags)
    , image_(image)
{
    //    this->setTooltip(image->getTooltip());
}

void ImageElement::addToContainer(MessageLayoutContainer &container,
                                  MessageElement::Flags flags)
{
    if (flags & this->getFlags()) {
        auto size = QSize(this->image_->width() * container.getScale(),
                          this->image_->height() * container.getScale());

        container.addElement((new ImageLayoutElement(*this, this->image_, size))
                                 ->setLink(this->getLink()));
    }
}

// EMOTE
EmoteElement::EmoteElement(const EmotePtr &emote, MessageElement::Flags flags)
    : MessageElement(flags)
    , emote_(emote)
{
    this->textElement_.reset(
        new TextElement(emote->getCopyString(), MessageElement::Misc));

    this->setTooltip(emote->tooltip.string);
}

EmotePtr EmoteElement::getEmote() const
{
    return this->emote_;
}

void EmoteElement::addToContainer(MessageLayoutContainer &container,
                                  MessageElement::Flags flags)
{
    if (flags & this->getFlags()) {
        if (flags & MessageElement::EmoteImages) {
            auto image = this->emote_->images.getImage(container.getScale());
            if (image->empty()) return;

            auto size = QSize(int(container.getScale() * image->width()),
                              int(container.getScale() * image->height()));

            container.addElement((new ImageLayoutElement(*this, image, size))
                                     ->setLink(this->getLink()));
        } else {
            if (this->textElement_) {
                this->textElement_->addToContainer(container,
                                                   MessageElement::Misc);
            }
        }
    }
}

// TEXT
TextElement::TextElement(const QString &text, MessageElement::Flags flags,
                         const MessageColor &color, FontStyle style)
    : MessageElement(flags)
    , color_(color)
    , style_(style)
{
    for (const auto &word : text.split(' ')) {
        this->words_.push_back({word, -1});
        // fourtf: add logic to store multiple spaces after message
    }
}

void TextElement::addToContainer(MessageLayoutContainer &container,
                                 MessageElement::Flags flags)
{
    auto app = getApp();

    if (flags & this->getFlags()) {
        QFontMetrics metrics =
            app->fonts->getFontMetrics(this->style_, container.getScale());

        for (Word &word : this->words_) {
            auto getTextLayoutElement = [&](QString text, int width,
                                            bool trailingSpace) {
                QColor color = this->color_.getColor(*app->themes);
                app->themes->normalizeColor(color);

                auto e = (new TextLayoutElement(
                              *this, text, QSize(width, metrics.height()),
                              color, this->style_, container.getScale()))
                             ->setLink(this->getLink());
                e->setTrailingSpace(trailingSpace);
                return e;
            };

            // fourtf: add again
            //            if (word.width == -1) {
            word.width = metrics.width(word.text);
            //            }

            // see if the text fits in the current line
            if (container.fitsInLine(word.width)) {
                container.addElementNoLineBreak(getTextLayoutElement(
                    word.text, word.width, this->hasTrailingSpace()));
                continue;
            }

            // see if the text fits in the next line
            if (!container.atStartOfLine()) {
                container.breakLine();

                if (container.fitsInLine(word.width)) {
                    container.addElementNoLineBreak(getTextLayoutElement(
                        word.text, word.width, this->hasTrailingSpace()));
                    continue;
                }
            }

            // we done goofed, we need to wrap the text
            QString text = word.text;
            int textLength = text.length();
            int wordStart = 0;
            int width = metrics.width(text[0]);

            for (int i = 1; i < textLength; i++) {
                int charWidth = metrics.width(text[i]);

                if (!container.fitsInLine(width + charWidth)) {
                    container.addElementNoLineBreak(getTextLayoutElement(
                        text.mid(wordStart, i - wordStart), width, false));
                    container.breakLine();

                    wordStart = i;
                    width = 0;
                    if (textLength > i + 2) {
                        width += metrics.width(text[i]);
                        width += metrics.width(text[i + 1]);
                        i += 1;
                    }
                    continue;
                }
                width += charWidth;
            }

            container.addElement(getTextLayoutElement(
                text.mid(wordStart), width, this->hasTrailingSpace()));
            container.breakLine();
        }
    }
}

// TIMESTAMP
TimestampElement::TimestampElement(QTime time)
    : MessageElement(MessageElement::Timestamp)
    , time_(time)
    , element_(this->formatTime(time))
{
    assert(this->element_ != nullptr);
}

void TimestampElement::addToContainer(MessageLayoutContainer &container,
                                      MessageElement::Flags flags)
{
    if (flags & this->getFlags()) {
        auto app = getApp();
        if (app->settings->timestampFormat != this->format_) {
            this->format_ = app->settings->timestampFormat.getValue();
            this->element_.reset(this->formatTime(this->time_));
        }

        this->element_->addToContainer(container, flags);
    }
}

TextElement *TimestampElement::formatTime(const QTime &time)
{
    static QLocale locale("en_US");

    QString format = locale.toString(time, getApp()->settings->timestampFormat);

    return new TextElement(format, Flags::Timestamp, MessageColor::System,
                           FontStyle::ChatMedium);
}

// TWITCH MODERATION
TwitchModerationElement::TwitchModerationElement()
    : MessageElement(MessageElement::ModeratorTools)
{
}

void TwitchModerationElement::addToContainer(MessageLayoutContainer &container,
                                             MessageElement::Flags flags)
{
    if (flags & MessageElement::ModeratorTools) {
        QSize size(int(container.getScale() * 16),
                   int(container.getScale() * 16));

        for (const auto &action :
             getApp()->moderationActions->items.getVector()) {
            if (auto image = action.getImage()) {
                container.addElement(
                    (new ImageLayoutElement(*this, image.get(), size))
                        ->setLink(Link(Link::UserAction, action.getAction())));
            } else {
                container.addElement(
                    (new TextIconLayoutElement(*this, action.getLine1(),
                                               action.getLine2(),
                                               container.getScale(), size))
                        ->setLink(Link(Link::UserAction, action.getAction())));
            }
        }
    }
}

}  // namespace chatterino
