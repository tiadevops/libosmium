#ifndef OSMIUM_IO_XML_OUTPUT_HPP
#define OSMIUM_IO_XML_OUTPUT_HPP

/*

This file is part of Osmium (http://osmcode.org/osmium).

Copyright 2013 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#define OSMIUM_COMPILE_WITH_CFLAGS_XML2 `xml2-config --cflags`
#define OSMIUM_LINK_WITH_LIBS_XML2 `xml2-config --libs`

// this is required to allow using libxml's xmlwriter in parallel to expat xml parser under debian
#undef XMLCALL
#include <libxml/xmlwriter.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <osmium/io/output.hpp>
#include <osmium/utils/timestamp.hpp>

namespace osmium {

    namespace io {

        struct XMLWriteError {};

        namespace {

            inline xmlChar* cast_to_xmlchar(const char* str) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
                return BAD_CAST str;
#pragma GCC diagnostic pop
            }

            inline void check_for_error(int count) {
                if (count < 0) {
                    throw XMLWriteError();
                }
            }

        }

        class XMLOutput : public osmium::io::Output, public osmium::handler::Handler<XMLOutput> {

            // objects of this class can't be copied
            XMLOutput(const XMLOutput&);
            XMLOutput& operator=(const XMLOutput&);

        public:

            XMLOutput(const osmium::io::File& file) :
                Output(file),
                m_xml_output_buffer(xmlOutputBufferCreateFd(this->fd(), NULL)),
                m_xml_writer(xmlNewTextWriter(m_xml_output_buffer)),
                m_last_op('\0') {
                if (!m_xml_output_buffer || !m_xml_writer) {
                    throw XMLWriteError();
                }
            }

            void handle_collection(osmium::memory::Buffer::const_iterator begin, osmium::memory::Buffer::const_iterator end) override {
                this->operator()(begin, end);
            }

            void set_meta(osmium::io::Meta& meta) override {
                check_for_error(xmlTextWriterSetIndent(m_xml_writer, 1));
                check_for_error(xmlTextWriterSetIndentString(m_xml_writer, cast_to_xmlchar("  ")));
                check_for_error(xmlTextWriterStartDocument(m_xml_writer, NULL, "UTF-8", NULL)); // <?xml .. ?>

                if (this->m_file.type() == osmium::io::FileType::Change()) {
                    check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("osmChange")));  // <osmChange>
                } else {
                    check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("osm")));  // <osm>
                }
                check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("version"), cast_to_xmlchar("0.6")));
                check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("generator"), cast_to_xmlchar(this->m_generator.c_str())));
                if (meta.bounds().defined()) {
                    check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("bounds"))); // <bounds>

                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("minlon"), "%.7f", meta.bounds().bottom_left().lon()));
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("minlat"), "%.7f", meta.bounds().bottom_left().lat()));
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("maxlon"), "%.7f", meta.bounds().top_right().lon()));
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("maxlat"), "%.7f", meta.bounds().top_right().lat()));

                    check_for_error(xmlTextWriterEndElement(m_xml_writer)); // </bounds>
                }
            }

            void node(const osmium::Node& node) {
                if (this->m_file.type() == osmium::io::FileType::Change()) {
                    open_close_op_tag(node.visible() ? (node.version() == 1 ? 'c' : 'm') : 'd');
                }
                check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("node"))); // <node>

                write_meta(node);

                if (node.location().defined()) {
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("lat"), "%.7f", node.location().lat()));
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("lon"), "%.7f", node.location().lon()));
                }

                write_tags(node.tags());

                check_for_error(xmlTextWriterEndElement(m_xml_writer)); // </node>
            }

            void way(const osmium::Way& way) {
                if (this->m_file.type() == osmium::io::FileType::Change()) {
                    open_close_op_tag(way.visible() ? (way.version() == 1 ? 'c' : 'm') : 'd');
                }
                check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("way"))); // <way>

                write_meta(way);

                for (auto& way_node : way.nodes()) {
                    check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("nd"))); // <nd>
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("ref"), "%" PRId64, way_node.ref()));
                    check_for_error(xmlTextWriterEndElement(m_xml_writer)); // </nd>
                }

                write_tags(way.tags());

                check_for_error(xmlTextWriterEndElement(m_xml_writer)); // </way>
            }

            void relation(const osmium::Relation& relation) {
                if (this->m_file.type() == osmium::io::FileType::Change()) {
                    open_close_op_tag(relation.visible() ? (relation.version() == 1 ? 'c' : 'm') : 'd');
                }
                check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("relation"))); // <relation>

                write_meta(relation);

                for (auto& member : relation.members()) {
                    check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("member"))); // <member>

                    check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("type"), cast_to_xmlchar(item_type_to_name(member.type()))));
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("ref"), "%" PRId64, member.ref()));
                    check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("role"), cast_to_xmlchar(member.role())));

                    check_for_error(xmlTextWriterEndElement(m_xml_writer)); // </member>
                }

                write_tags(relation.tags());

                check_for_error(xmlTextWriterEndElement(m_xml_writer)); // </relation>
            }

            void close() override {
                if (this->m_file.type() == osmium::io::FileType::Change()) {
                    open_close_op_tag('\0');
                }
                check_for_error(xmlTextWriterEndElement(m_xml_writer)); // </osm> or </osmChange>
                xmlFreeTextWriter(m_xml_writer);
                this->m_file.close();
            }

        private:

            xmlOutputBufferPtr m_xml_output_buffer;
            xmlTextWriterPtr m_xml_writer;
            char m_last_op;

            void write_meta(const osmium::Object& object) {
                check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("id"), "%" PRId64, object.id()));
                if (object.version()) {
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("version"), "%d", object.version()));
                }
                if (object.timestamp()) {
                    check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("timestamp"), cast_to_xmlchar(osmium::timestamp::to_iso(object.timestamp()).c_str())));
                }

                // uid <= 0 -> anonymous
                if (object.uid() > 0) {
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("uid"), "%d", object.uid()));
                    check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("user"), cast_to_xmlchar(object.user())));
                }

                if (object.changeset()) {
                    check_for_error(xmlTextWriterWriteFormatAttribute(m_xml_writer, cast_to_xmlchar("changeset"), "%d", object.changeset()));
                }

                if (this->m_file.has_multiple_object_versions() && this->m_file.type() != osmium::io::FileType::Change()) {
                    check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("visible"), object.visible() ? cast_to_xmlchar("true") : cast_to_xmlchar("false")));
                }
            }

            void write_tags(const osmium::TagList& tags) {
                for (auto& tag : tags) {
                    check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("tag"))); // <tag>
                    check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("k"), cast_to_xmlchar(tag.key())));
                    check_for_error(xmlTextWriterWriteAttribute(m_xml_writer, cast_to_xmlchar("v"), cast_to_xmlchar(tag.value())));
                    check_for_error(xmlTextWriterEndElement(m_xml_writer)); // </tag>
                }
            }

            void open_close_op_tag(const char op) {
                if (op == m_last_op) {
                    return;
                }

                if (m_last_op) {
                    check_for_error(xmlTextWriterEndElement(m_xml_writer));
                }

                switch (op) {
                    case 'c':
                        check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("create")));
                        break;
                    case 'm':
                        check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("modify")));
                        break;
                    case 'd':
                        check_for_error(xmlTextWriterStartElement(m_xml_writer, cast_to_xmlchar("delete")));
                        break;
                }

                m_last_op = op;
            }

        }; // class XMLOutput

        namespace {

            const bool registered_xml_output = osmium::io::OutputFactory::instance().register_output_format({
                osmium::io::Encoding::XML(),
                osmium::io::Encoding::XMLgz(),
                osmium::io::Encoding::XMLbz2()
            }, [](const osmium::io::File& file) {
                return new osmium::io::XMLOutput(file);
            });

        } // anonymous namespace

    } // namespace output

} // namespace osmium

#endif // OSMIUM_IO_XML_OUTPUT_HPP
