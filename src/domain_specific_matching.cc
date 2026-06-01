#include "domain_specific_matching.h"
#include <cctype>
#include <algorithm>

namespace {

std::string to_lower(std::string s) {
    for (char &c : s) c = std::tolower(c);
    return s;
}

} // namespace

bool matchDomainSpecificKeys(
    const std::string& tbl_a,
    const std::string& col_a,
    const std::string& type_a,
    const std::vector<std::string>& table_names,
    const std::unordered_map<std::string, TableInfo>& tables_info,
    std::set<Relationship>& relationships) {

    bool relationship_found = false;
    std::string col_lower = to_lower(col_a);
    if (col_lower == "asin" || col_lower == "upc" || col_lower == "ean" || col_lower == "isbn" || col_lower == "isbn-13") {
        for (const auto& tbl_b : table_names) {
            if (tbl_a == tbl_b) continue;
            if (to_lower(tbl_b) == "product_catalog" || to_lower(tbl_b) == "product" || to_lower(tbl_b) == "products") {
                auto it_b = tables_info.find(tbl_b);
                if (it_b != tables_info.end()) {
                    const auto& info_b = it_b->second;
                    // Check if target table has the same column and type compatible
                    for (const auto& col_b_pair : info_b.column_types) {
                        if (to_lower(col_b_pair.first) == col_lower) {
                            if (typeMatches(type_a, col_b_pair.second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = col_b_pair.first;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                            }
                        }
                    }
                }
            }
        }
    } else if (col_lower == "campaign_name") {
        // Find the appropriate campaign table
        std::string target_campaign_tbl = "";
        std::string tbl_a_lower = to_lower(tbl_a);
        if (tbl_a_lower.find("sponsored_products") != std::string::npos) {
            target_campaign_tbl = "ads_sponsored_products_campaigns_vc";
        } else if (tbl_a_lower.find("sponsored_brands_video") != std::string::npos) {
            target_campaign_tbl = "ads_sponsored_brands_video_campaign_vc";
        } else if (tbl_a_lower.find("sponsored_brands") != std::string::npos) {
            target_campaign_tbl = "ads_sponsored_brands_campaign_vc";
        } else if (tbl_a_lower.find("sponsored_display") != std::string::npos) {
            target_campaign_tbl = "ads_sponsored_display_campaign_vc";
        }
        
        if (!target_campaign_tbl.empty()) {
            for (const auto& tbl_b : table_names) {
                if (tbl_a == tbl_b) continue;
                if (to_lower(tbl_b) == target_campaign_tbl) {
                    auto it_b = tables_info.find(tbl_b);
                    if (it_b != tables_info.end()) {
                        const auto& info_b = it_b->second;
                        for (const auto& col_b_pair : info_b.column_types) {
                            if (to_lower(col_b_pair.first) == "campaign_name") {
                                if (typeMatches(type_a, col_b_pair.second)) {
                                    Relationship rel;
                                    rel.from_table = tbl_a;
                                    rel.from_column = col_a;
                                    rel.to_table = tbl_b;
                                    rel.to_column = col_b_pair.first;
                                    rel.is_explicit = false;
                                    relationships.insert(rel);
                                    relationship_found = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else if (col_lower == "deal_id") {
        for (const auto& tbl_b : table_names) {
            if (tbl_a == tbl_b) continue;
            if (to_lower(tbl_b) == "advertising_deals_summary") {
                auto it_b = tables_info.find(tbl_b);
                if (it_b != tables_info.end()) {
                    const auto& info_b = it_b->second;
                    for (const auto& col_b_pair : info_b.column_types) {
                        if (to_lower(col_b_pair.first) == "deal_id") {
                            if (typeMatches(type_a, col_b_pair.second)) {
                                Relationship rel;
                                rel.from_table = tbl_a;
                                rel.from_column = col_a;
                                rel.to_table = tbl_b;
                                rel.to_column = col_b_pair.first;
                                rel.is_explicit = false;
                                relationships.insert(rel);
                                relationship_found = true;
                            }
                        }
                    }
                }
            }
        }
    }
    return relationship_found;
}
